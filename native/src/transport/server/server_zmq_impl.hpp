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
    class server_zmq_impl
    {
    public:

        using listener = std::function<void(message)>;
        using internal_listener = trivial_messenger::listener;

        server_zmq_impl(zmq::context_t& context,
            const configuration& initial_config,
            kernel_configuration kernel_config,
            nl::json::error_handler_t eh,
            internal_listener listener);

        void start_publisher_thread();
        void start_heartbeat_thread();
        void stop_channels();

        void set_request_stop(bool stop);
        bool is_stopped() const;

        using message_channel = std::pair<message, channel>;
        std::optional<message_channel> poll_channels(long timeout);

        control_messenger& get_control_messenger();

        void send_shell(message message);
        void send_control(message message);
        std::optional<message> send_stdin(message message);
        void publish(pub_message message, channel c);

        void abort_queue(const listener& l, long polling_interval);
        void update_config(kernel_configuration& config) const;

        zmq::multipart_t serialize_iopub(pub_message&& msg);

    private:

        zmq::socket_t m_shell;
        zmq::socket_t m_controller;
        zmq::socket_t m_stdin;
        zmq::socket_t m_publisher_pub;
        zmq::socket_t m_publisher_controller;
        zmq::socket_t m_heartbeat_controller;

        using authentication_ptr = std::unique_ptr<authentication>;
        authentication_ptr p_auth;

        publisher m_publisher;
        heartbeat m_heartbeat;

        thread m_iopub_thread;
        thread m_hb_thread;

        trivial_messenger m_messenger;

        nl::json::error_handler_t m_error_handler;

        bool m_request_stop;
    };
}

#endif
