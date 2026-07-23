#ifndef DATASUITE_SERVER_ZMQ_IMPL_HPP
#define DATASUITE_SERVER_ZMQ_IMPL_HPP

#include <memory>

#include "zmq.hpp"
#include "zmq_addon.hpp"

#include "datasuite/context.hpp"
#include "datasuite/kernel_configuration.hpp"
#include "datasuite/server.hpp"

#include "datasuite/datasuite.hpp"
#include "datasuite/thread.hpp"

#include "../common/authentication.hpp"
#include "publisher.hpp"
#include "heartbeat.hpp"
#include "trivial_messenger.hpp"

namespace datasuite
{
    class ServerZmqImpl
    {
    public:

        using listener = std::function<void(Message)>;
        using internal_listener = TrivialMessenger::listener;

        ServerZmqImpl(zmq::context_t& context,
            const configuration& initial_config,
            KernelConfiguration kernel_config,
            json::error_handler_t eh,
            internal_listener listener);

        void startPublisherThread();
        void startHeartbeatThread();
        void stopChannels();

        void setRequestStop(bool stop);
        bool isStopped() const;

        using message_channel = std::pair<Message, channel>;
        std::optional<message_channel> pollChannels(long timeout);

        ControlMessenger& getControlMessenger();

        void sendShell(Message message);
        void sendControl(Message message);
        std::optional<Message> sendStdin(Message message);
        void publish(PubMessage message, channel c);

        void abortQueue(const listener& l, long polling_interval);
        void updateConfig(KernelConfiguration& config) const;

        zmq::multipart_t serializeIopub(PubMessage&& msg);

    private:

        zmq::socket_t m_shell;
        zmq::socket_t m_controller;
        zmq::socket_t m_stdin;
        zmq::socket_t m_publisherPub;
        zmq::socket_t m_publisherController;
        zmq::socket_t m_heartbeatController;

        using authentication_ptr = std::unique_ptr<Authentication>;
        authentication_ptr p_auth;

        Publisher m_publisher;
        Heartbeat m_heartbeat;

        Thread m_iopubThread;
        Thread m_hbThread;

        TrivialMessenger m_messenger;

        json::error_handler_t m_errorHandler;

        bool m_requestStop;
    };
}

#endif
