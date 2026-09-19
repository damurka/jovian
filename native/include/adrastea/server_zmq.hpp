#ifndef ADRASTEA_SERVER_ZMQ_HPP
#define ADRASTEA_SERVER_ZMQ_HPP

#include <optional>

#include "context.hpp"
#include "kernel_configuration.hpp"
#include "server.hpp"

#include "adrastea.hpp"

namespace adrastea
{
    class ServerZmqImpl;

    class ADRASTEA_API ServerZmq : public Server
    {
    public:

        ~ServerZmq() override;

        using Server::notifyInternalListener;

    protected:

        ServerZmq(Context& context,
            const configuration& config,
            json::error_handler_t eh);

        // API for inheriting classes
        void startPublisherThread();
        void startHeartbeatThread();
        void stopChannels();

        void setRequestStop(bool stop);
        bool isStopped() const;

        // The following methods must be called in the same thread
        using message_channel = std::pair<Message, channel>;
        std::optional<message_channel> pollChannels(long timeout);
        void sendShellMessage(Message msg);
        void sendControlMessage(Message msg);

    private:

        // Implementation of server virtual methods
        ControlMessenger& getControlMessengerImpl() override;

        void sendShellImpl(Message msg) override;
        void sendControlImpl(Message msg) override;
        void sendStdinImpl(Message msg) override;
        void publishImpl(PubMessage msg, channel c) override;

        void abortQueueImpl(const listener& l, long polling_interval) override;
        void updateConfigImpl(KernelConfiguration& config) const override;

        std::unique_ptr<ServerZmqImpl> p_impl;
    };

    ADRASTEA_API
    std::unique_ptr<Server> makeServerDefault(Context& context,
            const configuration& config,
            json::error_handler_t eh = json::error_handler_t::strict);
}

#endif
