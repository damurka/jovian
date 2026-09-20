#include "session_registry.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <thread>

#include "adrastea/guid.hpp"
#include "adrastea/message.hpp"
#include "adrastea/middleware.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace themisto
{
    namespace
    {
        // Unlike the addon (native/src/elara/bridge/engine.cpp's
        // setupEnvironment()), which can mutate PATH *after* the process
        // already started and still have it take effect (require()/dlopen
        // resolves the addon's own DLL imports at that later point), a
        // spawned elara.exe needs R's bin directory on PATH *before*
        // CreateProcess -- Windows resolves an EXE's own DLL imports (R.dll
        // etc, linked via R_LIBRARIES) at process/module load time, which
        // happens before main() (and hence setupEnvironment()) ever runs.
        // Session creation is already serialized on m_registrationMutex, so
        // mutating this process's own PATH here and leaving it prepended is
        // safe -- no concurrent createSessionWithId() can observe a
        // different rPath mid-spawn.
        void ensureRBinOnPath(const std::string& rHome, const std::string& rPath)
        {
            std::string rBinDir = !rPath.empty() ? rPath : (!rHome.empty() ? rHome + "/bin/x64" : std::string());
            if (rBinDir.empty())
            {
                return;
            }
            for (auto& c : rBinDir)
            {
                if (c == '/') c = '\\';
            }

#ifdef _WIN32
            char currentPath[32768] = { 0 };
            GetEnvironmentVariableA("PATH", currentPath, sizeof(currentPath));
            std::string path(currentPath);
            if (path.find(rBinDir) != std::string::npos)
            {
                return;
            }
            std::string newPath = rBinDir + ";" + path;
            SetEnvironmentVariableA("PATH", newPath.c_str());
#else
            const char* currentPath = std::getenv("PATH");
            std::string path = currentPath ? currentPath : "";
            if (path.find(rBinDir) != std::string::npos)
            {
                return;
            }
            std::string newPath = rBinDir + ":" + path;
            setenv("PATH", newPath.c_str(), 1);
#endif
        }
    }

    std::string toString(SessionStatus status)
    {
        switch (status)
        {
            case SessionStatus::Starting: return "starting";
            case SessionStatus::Ready: return "ready";
            case SessionStatus::Stopped: return "stopped";
            case SessionStatus::Crashed: return "crashed";
        }
        return "unknown";
    }

    Session::~Session()
    {
        polling = false;
        if (pollThread.joinable())
        {
            pollThread.join();
        }
    }

    void Session::emitMessage(const std::string& jsonText)
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (onMessage)
        {
            onMessage(jsonText);
        }
    }

    void Session::emitKernelExit(const std::string& reason)
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (onKernelExit)
        {
            onKernelExit(reason);
        }
    }

    SessionRegistry::SessionRegistry(std::map<std::string, std::string> kernelExePaths, std::string registrationIp)
        : m_kernelExePaths(std::move(kernelExePaths))
        , m_registrationIp(std::move(registrationIp))
    {
    }

    std::shared_ptr<std::recursive_mutex> SessionRegistry::getSessionOperationLock(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_sessionLocksMutex);
        auto it = m_sessionOperationLocks.find(id);
        if (it == m_sessionOperationLocks.end())
        {
            it = m_sessionOperationLocks.emplace(id, std::make_shared<std::recursive_mutex>()).first;
        }
        return it->second;
    }

    SessionRegistry::~SessionRegistry()
    {
        // Safety net only, for sessions the caller never explicitly
        // stopped -- skip anything stopSession() already tore down.
        // stopChannels()/kill() aren't safe to call twice: stopChannels()
        // signals and joins the client's iopub/heartbeat threads, and a
        // second call blocks forever sending a stop signal nothing is
        // listening for anymore (those threads already exited after the
        // first call), which hung this exact path before this guard.
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto& [id, session] : m_sessions)
        {
            if (session->status.load() == SessionStatus::Stopped)
            {
                continue;
            }
            if (session->client)
            {
                session->client->stopChannels();
            }
            if (session->process)
            {
                session->process->kill();
            }
        }
    }

    std::string SessionRegistry::startRegistrationListener()
    {
        m_registrationPort = adrastea::findFreePort();

        // One registration key for this supervisor's whole lifetime, shared
        // by every kernel it spawns. ClientHandshakeZmqImpl::waitForConfiguration()
        // (native/src/adrastea/transport/client/client_handshake_zmq.cpp) forces the
        // returned KernelConfiguration's key to match the *registration*
        // key it was constructed with -- i.e. per-session key rotation
        // isn't something the existing handshake protocol supports without
        // rebinding a fresh registration socket per session. This is still
        // a strict improvement over the addon's current hardcoded literal
        // ("shared-secret-key" in engine.cpp), and matches the
        // codebase's existing security posture rather than exceeding it.
        m_registrationKey = adrastea::newGuid().toString();

        adrastea::RegistrationConfiguration regConfig;
        regConfig.m_transport = "tcp";
        regConfig.m_signatureScheme = "hmac-sha256";
        regConfig.m_key = m_registrationKey;
        regConfig.m_registrationIp = m_registrationIp;
        regConfig.m_registrationPort = m_registrationPort;

        m_registrationContext = adrastea::makeZmqContext();
        m_registrationListener = std::make_unique<adrastea::ClientHandshakeZmq>(*m_registrationContext, regConfig);

        return m_registrationPort;
    }

    std::string SessionRegistry::createSession(SessionOptions options, std::string& error)
    {
        std::string id = adrastea::newGuid().toString();
        return createSessionWithId(id, std::move(options), error);
    }

    std::string SessionRegistry::createSessionWithId(const std::string& id, SessionOptions options, std::string& error)
    {
        auto exeIt = m_kernelExePaths.find(options.kernelType);
        if (exeIt == m_kernelExePaths.end() || exeIt->second.empty())
        {
            error = "no kernel executable is configured for kernelType '" + options.kernelType +
                    "' (this supervisor was started without one -- see main.cpp's kernel discovery)";
            return std::string();
        }

        auto session = std::make_shared<Session>();
        session->id = id;
        session->options = options;

        KernelProcessOptions procOptions;
        procOptions.kernelExePath = exeIt->second;
        procOptions.rHome = options.rHome;
        procOptions.rPath = options.rPath;
        procOptions.rLibs = options.rLibs;
        procOptions.pandocPath = options.pandocPath;
        procOptions.heraSrcPath = options.heraSrcPath;
        procOptions.pythonHome = options.pythonHome;
        procOptions.pythonPath = options.pythonPath;
        procOptions.venvPath = options.venvPath;
        procOptions.registrationIp = m_registrationIp;
        procOptions.registrationPort = m_registrationPort;

        adrastea::KernelConfiguration kernelConfig;
        try
        {
            // Serializes "spawn kernel, then receive its registration" as one
            // atomic step against the single shared registration socket --
            // ClientHandshakeZmqImpl::waitForConfiguration() has no built-in
            // per-caller correlation, so two concurrent createSession() calls
            // could otherwise each receive the *other's* kernel's handshake.
            //
            // waitForConfiguration() used to block on a plain (non-timeout)
            // zmq recv -- a kernel process that failed to start or crashed
            // before registering hung this call indefinitely rather than
            // surfacing an error (confirmed directly: a real createSession()
            // with no R_HOME configured never returned, leaving a live but
            // permanently-stuck themisto.exe behind). Fixed via an rcvtimeo
            // on the underlying router socket (see
            // ClientHandshakeZmqImpl's constructor, client_handshake_zmq.cpp)
            // -- this now throws a clear, actionable error instead.
            std::lock_guard<std::mutex> regLock(m_registrationMutex);

            // Reuse the supervisor's single registration key so the kernel
            // signs both its handshake and its subsequent channel traffic
            // with the key ClientHandshakeZmqImpl::waitForConfiguration()
            // will hand back as the resulting KernelConfiguration's key
            // (see the comment in startRegistrationListener()).
            procOptions.key = m_registrationKey;
            if (options.kernelType == "r")
            {
                ensureRBinOnPath(options.rHome, options.rPath);
            }
            session->process = std::make_unique<KernelProcess>(procOptions);
            session->process->start();

            // Lets waitForConfiguration() fail fast (a poll interval, not
            // the full timeout) the moment this specific process dies,
            // instead of always waiting out the timeout even when the
            // process itself already exited near-instantly (confirmed
            // directly: elara.exe exits in well under 100ms when R can't be
            // loaded, but this used to still take the full 60s to surface).
            KernelProcess* spawnedProcess = session->process.get();
            kernelConfig = m_registrationListener->waitForConfiguration(
                [spawnedProcess]() { return !spawnedProcess->isAlive(); });
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return std::string();
        }

        session->zmqContext = adrastea::makeZmqContext();
        session->client = adrastea::makeClientZmq(*session->zmqContext, kernelConfig);
        session->client->connect();
        session->client->start();

        // client->start() spawns ClientZmqImpl's iopub/heartbeat threads
        // (client_zmq_impl.cpp) but returns as soon as they're constructed,
        // not once they've reached their own listening loops. Those loops
        // are what ClientMessenger::stopChannels() (client_messenger.cpp)
        // signals via a REQ/REP "stop" round trip -- if stopSession() runs
        // fast enough after this (e.g. a session stopped almost immediately
        // after creation, with little else happening in between), the
        // "stop" REQ can be sent before the REP side is listening, and
        // ClientMessenger::stopChannels() then blocks forever on a reply
        // that was never going to come (found via
        // test/session_registry_test.cpp: an intermittent hang, present
        // only on fast create-then-stop sequences). No readiness signal
        // exists to wait on instead; this settle delay is a pragmatic
        // mitigation for a startup race in shared client code, not a fix
        // to it -- a real fix belongs in ClientIopub/ClientHeartbeat
        // themselves (e.g. signaling readiness before entering their loop).
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::weak_ptr<Session> weakSession = session;
        session->client->registerKernelStatusListener([weakSession](bool dead) {
            if (!dead)
            {
                return;
            }
            if (auto s = weakSession.lock())
            {
                // Already reported -- most commonly by pollLoop()'s own
                // faster OS-level exit check above, which usually wins this
                // race by tens of seconds for a process that actually
                // exited. Avoids sending the browser a second, redundant
                // kernelExit for the same session. A genuinely stuck-but-
                // still-running kernel (a deadlock, not an exit) is
                // unaffected: that case is never caught by pollLoop()'s
                // isAlive() check at all, only by this heartbeat timeout.
                if (s->status == SessionStatus::Crashed || s->status == SessionStatus::Stopped)
                {
                    return;
                }
                s->status = SessionStatus::Crashed;
                // Heartbeat timing out only tells us the kernel stopped
                // answering pings -- it looks identical whether the process
                // genuinely crashed or is merely stuck (deadlock, long
                // blocking native call). Cross-check against the OS process
                // handle to tell those apart before reporting.
                std::string reason = "heartbeat gave up waiting for a response";
                if (s->process)
                {
                    reason += " (" + s->process->describeStatus() + ")";
                }
                s->emitKernelExit(reason);
            }
        });

        session->status = SessionStatus::Ready;

        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions[id] = session;
        }

        startPolling(session);
        return id;
    }

    void SessionRegistry::startPolling(const std::shared_ptr<Session>& session)
    {
        session->polling = true;
        session->pollThread = std::thread(&SessionRegistry::pollLoop, this, session.get());
    }

    void SessionRegistry::pollLoop(Session* session)
    {
        auto* client = session->client.get();
        while (session->polling)
        {
            // Fast, OS-level dead-kernel detection -- independent of, and
            // far faster than, the ZMQ heartbeat's worst-case detection
            // window (4 failed round trips x 20s timeout =~ 60-80s, see
            // ClientHeartbeat/ClientZmqImpl's max_retry/heartbeat_timeout).
            // An externally killed process (Task Manager, a segfault,
            // os._exit()/quit()) is now visible within one poll interval
            // (~5ms) instead of up to 80s -- confirmed via a real repro:
            // without this, the browser saw nothing but its own client-side
            // execute timeout (30s) firing first, with the session only
            // flipping to "crashed" tens of seconds later once the
            // heartbeat finally gave up.
            //
            // Safe against racing an intentional stop()/restart():
            // stopSession() clears session->polling and joins this exact
            // thread BEFORE it ever kills the process or sets
            // SessionStatus::Stopped (see its own comment on that
            // ordering), so this loop is never still running while a
            // deliberate process kill is in flight -- if isAlive() is ever
            // false here, the kernel died on its own, not because we
            // stopped it.
            if (session->process && !session->process->isAlive())
            {
                session->status = SessionStatus::Crashed;
                session->emitKernelExit(
                    "kernel process exited unexpectedly (" + session->process->describeStatus() + ")");
                session->polling = false;
                break;
            }

            while (client->iopubQueueSize() > 0)
            {
                if (auto pubOpt = client->popIopubMessage())
                {
                    auto& msg = pubOpt.value();
                    // Field names are snake_case (msg_type/parent_msg_id),
                    // not the camelCase used elsewhere in this file, because
                    // this specific envelope shape is consumed as-is by the
                    // TS side's existing MessageParser.parse() (lib/messaging/
                    // message-parser.ts), which expects exactly these keys --
                    // reusing it outright instead of writing a second parser.
                    json envelope = {
                        { "type", "message" },
                        { "channel", "iopub" },
                        { "topic", msg.topic() },
                        { "msg_type", msg.header().value("msg_type", "") },
                        { "parent_msg_id", msg.parentHeader().value("msg_id", "") },
                        { "content", msg.content() }
                    };
                    session->emitMessage(envelope.dump());
                }
            }

            if (auto shellOpt = client->receiveOnShell(false))
            {
                auto& msg = shellOpt.value();
                json envelope = {
                    { "type", "message" },
                    { "channel", "shell" },
                    { "topic", msg.header().value("msg_type", "") },
                    { "msg_type", msg.header().value("msg_type", "") },
                    { "parent_msg_id", msg.parentHeader().value("msg_id", "") },
                    { "content", msg.content() }
                };
                session->emitMessage(envelope.dump());
            }

            // input_request: the interpreter blocked on e.g. R's readline()
            // or Python's input() (adrastea::blockingInputRequest(), which
            // genuinely blocks the kernel's own single execution thread on
            // a ZMQ recv -- see ClientZmqImpl's stdin channel comment).
            // Relayed the same way shell/iopub messages are; the WS client
            // answers via a "type": "inputReply" frame (ws_relay.cpp),
            // which reaches sendInputReply() below and is what actually
            // unblocks the kernel.
            if (auto stdinOpt = client->receiveOnStdin(false))
            {
                auto& msg = stdinOpt.value();
                json envelope = {
                    { "type", "message" },
                    { "channel", "stdin" },
                    { "topic", msg.header().value("msg_type", "") },
                    { "msg_type", msg.header().value("msg_type", "") },
                    { "parent_msg_id", msg.parentHeader().value("msg_id", "") },
                    { "content", msg.content() }
                };
                session->emitMessage(envelope.dump());
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    std::shared_ptr<Session> SessionRegistry::getSession(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(id);
        return it == m_sessions.end() ? nullptr : it->second;
    }

    json sessionToJson(const Session& session)
    {
        json result = {
            { "sessionId", session.id },
            { "status", toString(session.status.load()) },
            { "kernelType", session.options.kernelType },
            { "pid", session.process ? session.process->pid() : 0 }
        };
        std::optional<std::uint64_t> mem = session.process ? session.process->memoryUsageBytes() : std::nullopt;
        result["memoryBytes"] = mem.has_value() ? json(mem.value()) : json(nullptr);
        return result;
    }

    json SessionRegistry::listSessions()
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        json result = json::array();
        for (auto& [id, session] : m_sessions)
        {
            result.push_back(sessionToJson(*session));
        }
        return result;
    }

    bool SessionRegistry::sendExecute(const std::string& sessionId, const std::string& msgId, const std::string& code, const json& options)
    {
        // Same per-id lock stopSession()/restartSession() hold for their
        // entire duration -- without it, this could grab a session's
        // ClientZmq and call sendOnShell() on it concurrently with another
        // thread's stopChannels()/kill() tearing that same client down for
        // a stop or restart, a genuine data race on shared ZMQ socket
        // state, not just a stale-pointer issue (the shared_ptr keeps the
        // Session object alive either way). Acquiring the lock *before*
        // getSession() (not just wrapping the send) also means a call that
        // arrives mid-restart waits for the restart to finish and then
        // operates on whichever session is actually live afterward, rather
        // than risking a stale pointer to the one being replaced.
        std::lock_guard<std::recursive_mutex> opLock(*getSessionOperationLock(sessionId));

        auto session = getSession(sessionId);
        if (!session || !session->client)
        {
            return false;
        }

        json header = adrastea::makeHeader("execute_request", "client_user", sessionId);
        header["msg_id"] = msgId;

        json content = {
            { "code", code },
            { "silent", options.value("silent", false) },
            { "store_history", options.value("storeHistory", true) },
            { "user_expressions", json::object() },
            { "allow_stdin", options.value("allowStdin", false) }
        };

        adrastea::Message req({ "client_id" }, header, json::object(), json::object(), content, adrastea::buffer_sequence());
        session->client->sendOnShell(std::move(req));
        return true;
    }

    bool SessionRegistry::sendHistory(const std::string& sessionId, const std::string& msgId, const json& options)
    {
        // Same reasoning as sendExecute() above -- serialize against a
        // concurrent stop/restart of this same session.
        std::lock_guard<std::recursive_mutex> opLock(*getSessionOperationLock(sessionId));

        auto session = getSession(sessionId);
        if (!session || !session->client)
        {
            return false;
        }

        json header = adrastea::makeHeader("history_request", "client_user", sessionId);
        header["msg_id"] = msgId;

        // Matches the real Jupyter history_request spec (KernelCore::
        // historyRequest() -> HistoryManager::processRequest(), kernel_
        // core.cpp/history_manager.cpp) -- "tail" (n most recent) by
        // default, since that's what a client reconnecting to an
        // already-running kernel actually wants ("what did this kernel
        // already run"), not a full-range dump.
        json content = {
            { "hist_access_type", options.value("histAccessType", std::string("tail")) },
            { "output", options.value("output", false) },
            { "raw", options.value("raw", true) },
            { "n", options.value("n", 100) }
        };
        if (options.contains("session")) content["session"] = options["session"];
        if (options.contains("start")) content["start"] = options["start"];
        if (options.contains("stop")) content["stop"] = options["stop"];
        if (options.contains("pattern")) content["pattern"] = options["pattern"];
        if (options.contains("unique")) content["unique"] = options["unique"];

        adrastea::Message req({ "client_id" }, header, json::object(), json::object(), content, adrastea::buffer_sequence());
        session->client->sendOnShell(std::move(req));
        return true;
    }

    bool SessionRegistry::sendInterrupt(const std::string& sessionId, const std::string& msgId)
    {
        // Same reasoning as sendExecute() above -- serialize against a
        // concurrent stop/restart of this same session.
        std::lock_guard<std::recursive_mutex> opLock(*getSessionOperationLock(sessionId));

        auto session = getSession(sessionId);
        if (!session || !session->client)
        {
            return false;
        }

        json header = adrastea::makeHeader("interrupt_request", "client_user", sessionId);
        header["msg_id"] = msgId;

        adrastea::Message req({ "client_id" }, header, json::object(), json::object(), json::object(), adrastea::buffer_sequence());
        session->client->sendOnControl(std::move(req));
        return true;
    }

    bool SessionRegistry::sendInputReply(const std::string& sessionId, const std::string& value)
    {
        // Same reasoning as sendExecute()/sendInterrupt() above.
        std::lock_guard<std::recursive_mutex> opLock(*getSessionOperationLock(sessionId));

        auto session = getSession(sessionId);
        if (!session || !session->client)
        {
            return false;
        }

        // dispatchStdin() (kernel_core.cpp) only ever reads content.value(
        // "value", ...) -- it doesn't correlate against parent_header, since
        // ServerZmqImpl::sendStdin() already blocks the kernel's one
        // execution thread on this exact reply, so there's never more than
        // one outstanding input_request for a session to answer.
        json header = adrastea::makeHeader("input_reply", "client_user", sessionId);
        json content = { { "value", value } };

        adrastea::Message req({ "client_id" }, header, json::object(), json::object(), std::move(content), adrastea::buffer_sequence());
        session->client->sendOnStdin(std::move(req));
        return true;
    }

    bool SessionRegistry::stopSession(const std::string& id)
    {
        std::lock_guard<std::recursive_mutex> opLock(*getSessionOperationLock(id));

        auto session = getSession(id);
        if (!session)
        {
            return false;
        }

        if (session->client)
        {
            json shutHeader = adrastea::makeHeader("shutdown_request", "client_user", id);
            json shutContent = { { "restart", false } };
            adrastea::Message shutReq({ "client_id" }, shutHeader, json::object(), json::object(), shutContent, adrastea::buffer_sequence());
            session->client->sendOnControl(std::move(shutReq));
        }

        // Stop and join the poll thread *before* tearing down the client:
        // pollLoop() (below) calls client->iopubQueueSize()/popIopubMessage()/
        // receiveOnShell() concurrently from its own thread, so calling
        // client->stopChannels() first -- while that thread might still be
        // mid-call on the same ClientZmq -- raced the two against each
        // other and hung (found via test/session_registry_test.cpp; the
        // symptom was stopSession() itself never returning).
        session->polling = false;
        if (session->pollThread.joinable())
        {
            session->pollThread.join();
        }

        if (session->client)
        {
            session->client->stopChannels();
        }

        // Graceful-then-force, matching the addon's own shutdown handling
        // (lib/session/session-manager.ts's stop()/kill() split): give the
        // kernel a couple seconds to exit cleanly after shutdown_request
        // before force-killing the process. In practice this always hits
        // the force-kill path for a session running a Shiny app: the R
        // interpreter thread is permanently blocked inside
        // shiny::runApp() (see createShiny()'s "expected to block
        // indefinitely" comment) and never gets a chance to process a
        // control-channel shutdown_request -- verified via targeted
        // tracing, not just inferred. Confirmed harmless (the process is
        // reliably dead within ~2.3s either way), just worth knowing this
        // is the *normal* path for a Shiny session, not a sign anything's
        // stuck.
        for (int i = 0; i < 20 && session->process && session->process->isAlive(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (session->process && session->process->isAlive())
        {
            session->process->kill();
        }

        session->status = SessionStatus::Stopped;

        // A stopped session is permanently terminal -- unlike Crashed, it
        // can never come back (Session.restart() on the TS side refuses
        // outright once a session has been stopped; this is that same rule
        // enforced here too, not just trusted to the client). Nothing is
        // ever going to reference this id again, so there's no reason to
        // keep its ZMQ context (with its own IO threads), ClientZmq, and
        // KernelProcess alive for the rest of this themisto process's
        // lifetime -- unlike the "deliberately never erased" reasoning for
        // m_sessionOperationLocks (below), which is about giving every
        // caller for a given id the same canonical mutex OBJECT (erasing
        // and later recreating one under the same id would let two callers
        // each hold a *different* mutex while believing they have mutual
        // exclusion), Session objects have no such identity requirement --
        // erasing this map's own reference is safe under ordinary
        // shared_ptr semantics: anything still concurrently holding its own
        // copy (e.g. an in-flight WS ConnectionState) keeps the object
        // alive exactly as long as it needs it, then it's destroyed
        // normally once dropped, no dangling pointer possible.
        //
        // restartSession() (below) already independently erases and
        // recreates this same id's entry around its own stopSession() call,
        // so this doesn't change that path's behavior at all -- it's
        // already proven safe there (ConcurrentRestartsForTheSameSession
        // DontLeakAnExtraKernelProcess, ConcurrentExecuteDuringARestart
        // DoesNotCrashOrLeak); this just makes a genuinely standalone stop
        // (not a restart's internal one) release the same way.
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions.erase(id);
        }

        return true;
    }

    std::string SessionRegistry::restartSession(const std::string& id, std::string& error,
                                                 std::optional<SessionOptions> newOptions)
    {
        // Held for the *entire* stop-old/spawn-new sequence, not just the
        // map mutation below -- a second restartSession() call for this
        // same id (e.g. a user clicking "Restart" again before this one's
        // ~2-5s cycle finishes) blocks here until this one is completely
        // done, rather than the two racing to each stop-and-replace the
        // session independently. stopSession() re-enters this same
        // recursive_mutex from this same thread, which is fine.
        std::lock_guard<std::recursive_mutex> opLock(*getSessionOperationLock(id));

        auto session = getSession(id);
        if (!session)
        {
            error = "session not found";
            return std::string();
        }

        // A different rHome here (switching R versions/installations) is
        // exactly what lets a caller do that for an existing session in
        // place -- same session id, same WS URL -- instead of having to
        // stop this session and create a brand new one just to pick a
        // different R.
        SessionOptions options = newOptions.has_value() ? *newOptions : session->options;
        stopSession(id);

        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions.erase(id);
        }

        return createSessionWithId(id, options, error);
    }
}
