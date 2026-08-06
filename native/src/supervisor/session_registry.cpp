#include "session_registry.hpp"

#include <chrono>
#include <thread>

#include "datasuite/guid.hpp"
#include "datasuite/message.hpp"
#include "datasuite/middleware.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#endif

namespace datasuite::supervisor
{
    namespace
    {
        // Unlike the addon (native/src/bridge/datasuite_engine.cpp's
        // setupEnvironment()), which can mutate PATH *after* the process
        // already started and still have it take effect (require()/dlopen
        // resolves the addon's own DLL imports at that later point), a
        // spawned datasuite-r.exe needs R's bin directory on PATH *before*
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

    void Session::emitKernelExit()
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (onKernelExit)
        {
            onKernelExit();
        }
    }

    SessionRegistry::SessionRegistry(std::string kernelExePath, std::string registrationIp)
        : m_kernelExePath(std::move(kernelExePath))
        , m_registrationIp(std::move(registrationIp))
    {
    }

    SessionRegistry::~SessionRegistry()
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto& [id, session] : m_sessions)
        {
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
        m_registrationPort = datasuite::findFreePort();

        // One registration key for this supervisor's whole lifetime, shared
        // by every kernel it spawns. ClientHandshakeZmqImpl::waitForConfiguration()
        // (native/src/transport/client/client_handshake_zmq.cpp) forces the
        // returned KernelConfiguration's key to match the *registration*
        // key it was constructed with -- i.e. per-session key rotation
        // isn't something the existing handshake protocol supports without
        // rebinding a fresh registration socket per session. This is still
        // a strict improvement over the addon's current hardcoded literal
        // ("shared-secret-key" in datasuite_engine.cpp), and matches the
        // codebase's existing security posture rather than exceeding it.
        m_registrationKey = datasuite::newGuid().toString();

        datasuite::RegistrationConfiguration regConfig;
        regConfig.m_transport = "tcp";
        regConfig.m_signatureScheme = "hmac-sha256";
        regConfig.m_key = m_registrationKey;
        regConfig.m_registrationIp = m_registrationIp;
        regConfig.m_registrationPort = m_registrationPort;

        m_registrationContext = datasuite::makeZmqContext();
        m_registrationListener = std::make_unique<datasuite::ClientHandshakeZmq>(*m_registrationContext, regConfig);

        return m_registrationPort;
    }

    std::string SessionRegistry::createSession(SessionOptions options, std::string& error)
    {
        std::string id = datasuite::newGuid().toString();
        return createSessionWithId(id, std::move(options), error);
    }

    std::string SessionRegistry::createSessionWithId(const std::string& id, SessionOptions options, std::string& error)
    {
        auto session = std::make_shared<Session>();
        session->id = id;
        session->options = options;

        KernelProcessOptions procOptions;
        procOptions.kernelExePath = m_kernelExePath;
        procOptions.rHome = options.rHome;
        procOptions.rPath = options.rPath;
        procOptions.rLibs = options.rLibs;
        procOptions.pandocPath = options.pandocPath;
        procOptions.heraSrcPath = options.heraSrcPath;
        procOptions.registrationIp = m_registrationIp;
        procOptions.registrationPort = m_registrationPort;

        datasuite::KernelConfiguration kernelConfig;
        try
        {
            // Serializes "spawn kernel, then receive its registration" as one
            // atomic step against the single shared registration socket --
            // ClientHandshakeZmqImpl::waitForConfiguration() has no built-in
            // per-caller correlation, so two concurrent createSession() calls
            // could otherwise each receive the *other's* kernel's handshake.
            //
            // Known limitation: waitForConfiguration() blocks on a plain
            // (non-timeout) zmq recv -- a kernel process that fails to start
            // or crashes before registering will hang this call indefinitely
            // rather than surfacing an error. Not fixed in this pass since it
            // requires changing already-shared handshake code; flagged here
            // for follow-up (e.g. adding an rcvtimeo to the underlying
            // router socket).
            std::lock_guard<std::mutex> regLock(m_registrationMutex);

            // Reuse the supervisor's single registration key so the kernel
            // signs both its handshake and its subsequent channel traffic
            // with the key ClientHandshakeZmqImpl::waitForConfiguration()
            // will hand back as the resulting KernelConfiguration's key
            // (see the comment in startRegistrationListener()).
            procOptions.key = m_registrationKey;
            ensureRBinOnPath(options.rHome, options.rPath);
            session->process = std::make_unique<KernelProcess>(procOptions);
            session->process->start();

            kernelConfig = m_registrationListener->waitForConfiguration();
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return std::string();
        }

        session->zmqContext = datasuite::makeZmqContext();
        session->client = datasuite::makeClientZmq(*session->zmqContext, kernelConfig);
        session->client->connect();
        session->client->start();

        std::weak_ptr<Session> weakSession = session;
        session->client->registerKernelStatusListener([weakSession](bool dead) {
            if (!dead)
            {
                return;
            }
            if (auto s = weakSession.lock())
            {
                s->status = SessionStatus::Crashed;
                s->emitKernelExit();
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

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    std::shared_ptr<Session> SessionRegistry::getSession(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(id);
        return it == m_sessions.end() ? nullptr : it->second;
    }

    json SessionRegistry::listSessions()
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        json result = json::array();
        for (auto& [id, session] : m_sessions)
        {
            result.push_back({ { "sessionId", id }, { "status", toString(session->status.load()) } });
        }
        return result;
    }

    bool SessionRegistry::sendExecute(const std::string& sessionId, const std::string& msgId, const std::string& code, const json& options)
    {
        auto session = getSession(sessionId);
        if (!session || !session->client)
        {
            return false;
        }

        json header = datasuite::makeHeader("execute_request", "client_user", sessionId);
        header["msg_id"] = msgId;

        json content = {
            { "code", code },
            { "silent", options.value("silent", false) },
            { "store_history", options.value("storeHistory", true) },
            { "user_expressions", json::object() },
            { "allow_stdin", options.value("allowStdin", false) }
        };

        datasuite::Message req({ "client_id" }, header, json::object(), json::object(), content, datasuite::buffer_sequence());
        session->client->sendOnShell(std::move(req));
        return true;
    }

    bool SessionRegistry::sendInterrupt(const std::string& sessionId, const std::string& msgId)
    {
        auto session = getSession(sessionId);
        if (!session || !session->client)
        {
            return false;
        }

        json header = datasuite::makeHeader("interrupt_request", "client_user", sessionId);
        header["msg_id"] = msgId;

        datasuite::Message req({ "client_id" }, header, json::object(), json::object(), json::object(), datasuite::buffer_sequence());
        session->client->sendOnControl(std::move(req));
        return true;
    }

    bool SessionRegistry::stopSession(const std::string& id)
    {
        auto session = getSession(id);
        if (!session)
        {
            return false;
        }

        if (session->client)
        {
            json shutHeader = datasuite::makeHeader("shutdown_request", "client_user", id);
            json shutContent = { { "restart", false } };
            datasuite::Message shutReq({ "client_id" }, shutHeader, json::object(), json::object(), shutContent, datasuite::buffer_sequence());
            session->client->sendOnControl(std::move(shutReq));
            session->client->stopChannels();
        }

        // Graceful-then-force, matching the addon's own shutdown handling
        // (lib/session/session-manager.ts's stop()/kill() split): give the
        // kernel a couple seconds to exit cleanly after shutdown_request
        // before force-killing the process.
        for (int i = 0; i < 20 && session->process && session->process->isAlive(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (session->process && session->process->isAlive())
        {
            session->process->kill();
        }

        session->polling = false;
        session->status = SessionStatus::Stopped;
        return true;
    }

    std::string SessionRegistry::restartSession(const std::string& id, std::string& error)
    {
        auto session = getSession(id);
        if (!session)
        {
            error = "session not found";
            return std::string();
        }

        SessionOptions options = session->options;
        stopSession(id);

        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions.erase(id);
        }

        return createSessionWithId(id, options, error);
    }
}
