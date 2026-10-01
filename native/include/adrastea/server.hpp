#ifndef ADRASTEA_SERVER_HPP
#define ADRASTEA_SERVER_HPP

#include <functional>

#include "control_messenger.hpp"
#include "adrastea.hpp"
#include "kernel_configuration.hpp"
#include "message.hpp"

namespace adrastea
{
    enum class channel
    {
        SHELL,
        CONTROL
    };

    class ADRASTEA_API Server
    {
    public:
        using listener = std::function<void(Message)>;
        using internal_listener = std::function<json(json)>;

        virtual ~Server() = default;

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        Server(Server&&) = delete;
        Server& operator=(Server&&) = delete;

        ControlMessenger& getControlMessenger();

        void sendShell(Message message);
        void sendControl(Message message);
        void sendStdin(Message message);
        void publish(PubMessage message, channel c);

        // Bracket every code execution (KernelCore::executeRequest). A server
        // that can service control requests WHILE code runs -- interrupt is
        // the reason -- uses these to start/stop doing so; the default does
        // nothing (control messages then wait for the execution to finish).
        void beginExecution();
        void endExecution();

        void start(PubMessage message);
        void abortQueue(const listener& l, long polling_interval);
        void stop();
        void updateConfig(KernelConfiguration& config) const;

        void registerShellListener(const listener& l);
        void registerControlListener(const listener& l);
        void registerStdinListener(const listener& l);
        void registerInternalListener(const internal_listener& l);

        // Which shell requests (by msg_type) may be answered WHILE code runs,
        // on the thread that services the control channel then, instead of
        // waiting for the execution to finish -- see
        // Interpreter::answersWhileBusy(). Without a filter, none are. A
        // server that cannot read the shell channel during an execution
        // ignores it.
        using busy_filter = std::function<bool(const std::string&)>;
        void registerBusyShellFilter(const busy_filter& f);
        bool answersWhileBusy(const std::string& msg_type) const;

        // Called on the kernel thread, every few milliseconds, while no
        // request is waiting -- the interpreter's chance to run its own event
        // loop between requests (Interpreter::idle()).
        using idle_listener = std::function<void()>;
        void registerIdleListener(const idle_listener& l);

        // For an interpreter whose language runs its own event loop (R's REPL, which the R kernel lets own the main
        // thread, as Ark does): start() sets the channels up, then calls this instead of running the server's loop,
        // and the language's loop calls pollOnce() whenever it waits for input.
        using main_loop = std::function<void()>;
        void setMainLoop(main_loop loop);

        // One turn of the server's loop: the next shell or control request handled, or the idle work done, waiting
        // at most timeoutMs for a request. False once the server is stopped (a shutdown_request): its channels are
        // closed by then, so what was sent has gone out.
        bool pollOnce(long timeoutMs);

    protected:

        Server() = default;

        void notifyIdle();
        main_loop m_mainLoop;
        void notifyShellListener(Message msg);
        void notifyControlListener(Message msg);
        void notifyStdinListener(Message msg);
        json notifyInternalListener(json msg);

    private:

        virtual ControlMessenger& getControlMessengerImpl() = 0;

        virtual void sendShellImpl(Message message) = 0;
        virtual void sendControlImpl(Message message) = 0;
        virtual void sendStdinImpl(Message message) = 0;
        virtual void publishImpl(PubMessage message, channel c) = 0;

        virtual void beginExecutionImpl() {}
        virtual void endExecutionImpl() {}

        virtual void startImpl(PubMessage message) = 0;
        virtual void abortQueueImpl(const listener& l, long polling_interval) = 0;
        virtual void stopImpl() = 0;
        virtual bool pollOnceImpl(long /*timeoutMs*/) { return false; }
        virtual void updateConfigImpl(KernelConfiguration& config) const = 0;

        listener m_shellListener;
        listener m_controlListener;
        listener m_stdinListener;
        internal_listener m_internalListener;
        busy_filter m_busyFilter;
        idle_listener m_idleListener;
    };
}


#endif // ADRASTEA_SERVER_HPP
