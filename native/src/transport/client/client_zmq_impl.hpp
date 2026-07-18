#ifndef DATASUITE_CLIENT_ZMQ_IMPL_HPP
#define DATASUITE_CLIENT_ZMQ_IMPL_HPP

#include <nlohmann/json.hpp>
#include "zmq.hpp"

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
            const kernel_configuration& config,
            nl::json::error_handler_t eh);

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

        dealer_channel m_shell_client;
        dealer_channel m_control_client;
        client_iopub m_iopub_client;
        client_heartbeat m_heartbeat_client;

        client_messenger p_messenger;

        nl::json::error_handler_t m_error_handler;

        listener m_shell_listener;
        listener m_control_listener;
        iopub_listener m_iopub_listener;

        thread m_iopub_thread;
        thread m_heartbeat_thread;
    };
}

#endif
