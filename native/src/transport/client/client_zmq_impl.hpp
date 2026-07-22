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
    class authentication;

    class client_zmq_impl
    {
    public:
        using listener = std::function<void(message)>;
        using iopub_listener = std::function<void(pub_message)>;
        using kernel_status_listener = std::function<void(bool)>;

        client_zmq_impl(zmq::context_t& context,
            const KernelConfiguration& config,
            json::error_handler_t eh);

        ~client_zmq_impl();

        client_zmq_impl(const client_zmq_impl&) = delete;
        client_zmq_impl& operator=(const client_zmq_impl&) = delete;

        client_zmq_impl(client_zmq_impl&&) = delete;
        client_zmq_impl& operator=(client_zmq_impl&&) = delete;

        // shell channel
        void send_on_shell(message msg);
        std::optional<message> receive_on_shell(bool blocking);
        void register_shell_listener(const listener& l);

        // control channel
        void send_on_control(message msg);
        std::optional<message> receive_on_control(bool blocking);
        void register_control_listener(const listener& l);

        // iopub channel
        std::size_t iopub_queue_size() const;
        std::optional<pub_message> pop_iopub_message();
        void register_iopub_listener(const iopub_listener& l);

        // heartbeat channel
        void register_kernel_status_listener(const kernel_status_listener& l);

        // client messenger
        void connect();
        void stop_channels();

        void wait_for_message();
        void start();

        message deserialize(zmq::multipart_t& wire_msg) const;
        pub_message deserialize_iopub(zmq::multipart_t& wire_msg) const;

    private:

        void start_iopub_thread();
        void start_heartbeat_thread();
        void poll(long timeout);

        void notify_shell_listener(message msg);
        void notify_control_listener(message msg);
        void notify_iopub_listener(pub_message msg);
        void notify_kernel_dead(bool status);

        using authentication_ptr = std::unique_ptr<authentication>;
        authentication_ptr p_auth;

        dealer_channel m_shellClient;
        dealer_channel m_controlClient;
        client_iopub m_iopubClient;
        client_heartbeat m_heartbeatClient;

        client_messenger p_messenger;

        json::error_handler_t m_errorHandler;

        listener m_shellListener;
        listener m_controlListener;
        iopub_listener m_iopubListener;

        thread m_iopubThread;
        thread m_heartbeatThread;
    };
}

#endif
