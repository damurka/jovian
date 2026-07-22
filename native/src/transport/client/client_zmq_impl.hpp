#ifndef DATASUITE_CLIENT_ZMQ_IMPL_HPP
#define DATASUITE_CLIENT_ZMQ_IMPL_HPP

#include "zmq.hpp"

#include "datasuite/json.hpp"
#include "datasuite/context.hpp"
#include "datasuite/kernel_configuration.hpp"
#include "datasuite/message.hpp"

#include "datasuite/thread.hpp"

#include "dealer_channel.hpp"
#include "client_iopub.hpp"
#include "client_heartbeat.hpp"
#include "client_messenger.hpp"

namespace datasuite
{
    class Authentication;

    class ClientZmqImpl
    {
    public:
        using listener = std::function<void(Message)>;
        using iopub_listener = std::function<void(PubMessage)>;
        using kernel_status_listener = std::function<void(bool)>;

        ClientZmqImpl(zmq::context_t& context,
            const KernelConfiguration& config,
            json::error_handler_t eh);

        ~ClientZmqImpl();

        ClientZmqImpl(const ClientZmqImpl&) = delete;
        ClientZmqImpl& operator=(const ClientZmqImpl&) = delete;

        ClientZmqImpl(ClientZmqImpl&&) = delete;
        ClientZmqImpl& operator=(ClientZmqImpl&&) = delete;

        // shell channel
        void send_on_shell(Message msg);
        std::optional<Message> receive_on_shell(bool blocking);
        void register_shell_listener(const listener& l);

        // control channel
        void send_on_control(Message msg);
        std::optional<Message> receive_on_control(bool blocking);
        void register_control_listener(const listener& l);

        // iopub channel
        std::size_t iopub_queue_size() const;
        std::optional<PubMessage> pop_iopub_message();
        void register_iopub_listener(const iopub_listener& l);

        // heartbeat channel
        void register_kernel_status_listener(const kernel_status_listener& l);

        // client messenger
        void connect();
        void stop_channels();

        void wait_for_message();
        void start();

        Message deserialize(zmq::multipart_t& wire_msg) const;
        PubMessage deserialize_iopub(zmq::multipart_t& wire_msg) const;

    private:

        void start_iopub_thread();
        void start_heartbeat_thread();
        void poll(long timeout);

        void notify_shell_listener(Message msg);
        void notify_control_listener(Message msg);
        void notify_iopub_listener(PubMessage msg);
        void notify_kernel_dead(bool status);

        using authentication_ptr = std::unique_ptr<Authentication>;
        authentication_ptr p_auth;

        DealerChannel m_shellClient;
        DealerChannel m_controlClient;
        ClientIopub m_iopubClient;
        ClientHeartbeat m_heartbeatClient;

        ClientMessenger p_messenger;

        json::error_handler_t m_errorHandler;

        listener m_shellListener;
        listener m_controlListener;
        iopub_listener m_iopubListener;

        Thread m_iopubThread;
        Thread m_heartbeatThread;
    };
}

#endif
