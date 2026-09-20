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

    protected:

        Server() = default;

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
        virtual void updateConfigImpl(KernelConfiguration& config) const = 0;

        listener m_shellListener;
        listener m_controlListener;
        listener m_stdinListener;
        internal_listener m_internalListener;
    };
}


#endif // ADRASTEA_SERVER_HPP
