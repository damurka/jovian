#ifndef THEMISTO_SESSION_REGISTRY_HPP
#define THEMISTO_SESSION_REGISTRY_HPP

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "adrastea/context.hpp"
#include "adrastea/json.hpp"

#include "adrastea/transport/client/client_handshake_zmq.hpp"
#include "adrastea/transport/client/client_zmq.hpp"

#include "kernel_process.hpp"

namespace themisto
{
    using json = adrastea::json;

    struct SessionOptions
    {
        // "r" (default, for every existing caller that predates this field)
        // or "python" -- selects which kernel executable createSessionWithId()
        // spawns (SessionRegistry::m_kernelExePaths) and which of the two
        // field groups below actually gets used. Not an enum: it round-trips
        // through JSON (http_api.cpp) and a plain string keeps that trivial
        // and keeps SessionRegistry from needing to know about new kernel
        // types beyond adding another map entry.
        std::string kernelType = "r";

        std::string rHome;
        std::string rPath;
        std::string rLibs;
        std::string pandocPath;
        std::string heraSrcPath;

        // Carpo (Python) equivalents, mirroring carpo::EnvironmentConfig.
        std::string pythonHome;
        std::string pythonPath;
        std::string venvPath;
    };

    enum class SessionStatus
    {
        Starting,
        Ready,
        Stopped,
        Crashed
    };

    std::string toString(SessionStatus status);

    class Session;

    // The one, shared "what does a session look like over HTTP" JSON
    // shape -- used by both SessionRegistry::listSessions() and
    // http_api.cpp's single-session GET, so the two can't drift out of
    // sync (they briefly had: the single-session route was missing
    // kernelType for a while after listSessions() already had it).
    // pid/memoryBytes are best-effort (KernelProcess::pid()/
    // memoryUsageBytes()) -- 0/omitted if the process isn't running or
    // memory reporting isn't implemented for this platform.
    json sessionToJson(const Session& session);

    // One R kernel session: the spawned elara process plus the ZMQ
    // client connected to it. Message traffic (execute/interrupt replies,
    // iopub output) is pumped out via `onMessage`, set by whichever
    // WebSocket connection is currently attached to this session
    // (ws_relay.cpp) -- there is at most one at a time, mirroring one
    // Session object client-side owning one WS connection.
    class Session
    {
    public:
        std::string id;
        SessionOptions options;
        std::unique_ptr<KernelProcess> process;
        std::unique_ptr<adrastea::Context> zmqContext;
        std::unique_ptr<adrastea::ClientZmq> client;
        std::atomic<SessionStatus> status{ SessionStatus::Starting };

        std::mutex callbackMutex;
        std::function<void(const std::string&)> onMessage;
        std::function<void(const std::string&)> onKernelExit;

        std::atomic<bool> polling{ false };
        std::thread pollThread;

        ~Session();

        void emitMessage(const std::string& jsonText);
        void emitKernelExit(const std::string& reason);
    };

    class SessionRegistry
    {
    public:
        // kernelExePaths maps SessionOptions::kernelType ("r", "python") to
        // the executable to spawn for that type -- main.cpp discovers both
        // elara and carpo as siblings and builds this map once at startup.
        // A type with no entry (or one whose file doesn't exist -- carpo
        // isn't built by default, see JOVIAN_BUILD_CARPO) fails the
        // individual createSession() call for that type with a clear error
        // rather than the whole supervisor refusing to start.
        SessionRegistry(std::map<std::string, std::string> kernelExePaths, std::string registrationIp);
        ~SessionRegistry();

        // Binds the shared registration listener. Must be called once
        // before any createSession(). Returns the bound registration port.
        std::string startRegistrationListener();

        // Blocks until the spawned kernel registers (or times out).
        std::string createSession(SessionOptions options, std::string& error);

        std::shared_ptr<Session> getSession(const std::string& id);
        json listSessions();

        bool stopSession(const std::string& id);

        // Replaces the kernel process under the same session id, same as a
        // no-argument restart, but if `newOptions` is given (e.g. a
        // different rHome -- switching R versions) it's used in place of
        // the session's original options instead of reusing them. This is
        // what lets a client switch R installations for an existing
        // session/notebook connection without tearing it down and creating
        // a brand new one (a different session id, WS URL, etc.) just to
        // pick a different R version.
        std::string restartSession(const std::string& id, std::string& error,
                                    std::optional<SessionOptions> newOptions = std::nullopt);

        bool sendExecute(const std::string& sessionId, const std::string& msgId, const std::string& code, const json& options);
        bool sendInterrupt(const std::string& sessionId, const std::string& msgId);

        // Sends a real Jupyter history_request on the shell channel --
        // KernelCore::historyRequest() (kernel_core.cpp) already handles it
        // and replies with a genuine history_reply; this was simply never
        // reachable from anywhere outside a raw Jupyter client connecting
        // directly to the kernel's own ZMQ ports until now. `options` may
        // carry histAccessType ("tail"/"range"/"search", default "tail"),
        // output, raw, n, session, start, stop, pattern, unique -- see
        // HistoryManager::processRequest()'s own field reads for the exact
        // per-access-type set. Reply is relayed like any other shell
        // message (ws_relay.cpp/SessionRegistry::pollLoop()'s existing
        // receiveOnShell() polling), not returned synchronously here.
        bool sendHistory(const std::string& sessionId, const std::string& msgId, const json& options);

        // Answers a real, currently-blocked input_request from this
        // session's kernel (R's readline()/Python's input(), see
        // adrastea::blockingInputRequest()) -- the one thing that actually
        // unblocks it, since ServerZmqImpl::sendStdin() (kernel side) is a
        // genuine blocking ZMQ recv waiting for exactly this.
        bool sendInputReply(const std::string& sessionId, const std::string& value);

    private:
        std::string createSessionWithId(const std::string& id, SessionOptions options, std::string& error);

        // Serializes every stop/restart operation targeting a given session
        // id against every other one targeting the *same* id (a fresh id
        // from createSession() never collides with anything, so that path
        // doesn't need this). Without it, two overlapping restart calls for
        // the same id (e.g. a user clicking "Restart" again before the
        // first click's ~2-5s cycle finishes -- confirmed via a real repro,
        // not hypothetical) each independently stop-the-old/spawn-a-new
        // kernel and race to write m_sessions[id], leaving the loser's
        // freshly spawned kernel process orphaned outside the map with
        // nothing left to ever stop it, plus a corrupted/truncated HTTP
        // response for whichever request lost the race. A std::recursive_
        // mutex because restartSession() calls stopSession() on the same
        // id from the same thread while already holding this lock.
        std::shared_ptr<std::recursive_mutex> getSessionOperationLock(const std::string& id);
        void startPolling(const std::shared_ptr<Session>& session);
        // Raw pointer, deliberately not shared_ptr: the poll thread must not
        // hold a strong reference to the Session it's polling, or the
        // Session's own destructor (which joins this thread) could never
        // run -- ~Session() runs once the last shared_ptr copy is dropped,
        // but this thread would still be holding one, so it would wait
        // forever for a join that can't start. The thread only ever runs
        // while the session is alive: Session::~Session() joins it before
        // any member teardown happens, and startPolling() is only called
        // while the session is (transitively) kept alive by m_sessions.
        void pollLoop(Session* session);

        std::map<std::string, std::string> m_kernelExePaths;
        std::string m_registrationIp;
        std::string m_registrationPort;
        std::string m_registrationKey;

        std::unique_ptr<adrastea::Context> m_registrationContext;
        std::unique_ptr<adrastea::ClientHandshakeZmq> m_registrationListener;
        std::mutex m_registrationMutex;

        std::mutex m_sessionsMutex;
        std::map<std::string, std::shared_ptr<Session>> m_sessions;

        // Deliberately never erased once created for a given id -- a
        // supervisor process's lifetime and the number of distinct session
        // ids it ever sees are both small/local-dev-scale, so trading a
        // tiny amount of unbounded (but bounded-by-actual-usage) memory for
        // never having to reason about removing a lock while another
        // thread might still be about to use it is the right call here.
        std::mutex m_sessionLocksMutex;
        std::map<std::string, std::shared_ptr<std::recursive_mutex>> m_sessionOperationLocks;
    };
}

#endif
