#ifndef DATASUITE_SUPERVISOR_SESSION_REGISTRY_HPP
#define DATASUITE_SUPERVISOR_SESSION_REGISTRY_HPP

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "datasuite/context.hpp"
#include "datasuite/json.hpp"

#include "transport/client/client_handshake_zmq.hpp"
#include "transport/client/client_zmq.hpp"

#include "kernel_process.hpp"

namespace datasuite::supervisor
{
    using json = datasuite::json;

    struct SessionOptions
    {
        std::string rHome;
        std::string rPath;
        std::string rLibs;
        std::string pandocPath;
        std::string heraSrcPath;
    };

    enum class SessionStatus
    {
        Starting,
        Ready,
        Stopped,
        Crashed
    };

    std::string toString(SessionStatus status);

    // One R kernel session: the spawned datasuite-r process plus the ZMQ
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
        std::unique_ptr<datasuite::Context> zmqContext;
        std::unique_ptr<datasuite::ClientZmq> client;
        std::atomic<SessionStatus> status{ SessionStatus::Starting };

        std::mutex callbackMutex;
        std::function<void(const std::string&)> onMessage;
        std::function<void()> onKernelExit;

        std::atomic<bool> polling{ false };
        std::thread pollThread;

        ~Session();

        void emitMessage(const std::string& jsonText);
        void emitKernelExit();
    };

    class SessionRegistry
    {
    public:
        SessionRegistry(std::string kernelExePath, std::string registrationIp);
        ~SessionRegistry();

        // Binds the shared registration listener. Must be called once
        // before any createSession(). Returns the bound registration port.
        std::string startRegistrationListener();

        // Blocks until the spawned kernel registers (or times out).
        std::string createSession(SessionOptions options, std::string& error);

        std::shared_ptr<Session> getSession(const std::string& id);
        json listSessions();

        bool stopSession(const std::string& id);
        std::string restartSession(const std::string& id, std::string& error);

        bool sendExecute(const std::string& sessionId, const std::string& msgId, const std::string& code, const json& options);
        bool sendInterrupt(const std::string& sessionId, const std::string& msgId);

    private:
        std::string createSessionWithId(const std::string& id, SessionOptions options, std::string& error);
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

        std::string m_kernelExePath;
        std::string m_registrationIp;
        std::string m_registrationPort;
        std::string m_registrationKey;

        std::unique_ptr<datasuite::Context> m_registrationContext;
        std::unique_ptr<datasuite::ClientHandshakeZmq> m_registrationListener;
        std::mutex m_registrationMutex;

        std::mutex m_sessionsMutex;
        std::map<std::string, std::shared_ptr<Session>> m_sessions;
    };
}

#endif
