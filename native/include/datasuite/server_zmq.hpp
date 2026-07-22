#ifndef DATASUITE_SERVER_ZMQ_HPP
#define DATASUITE_SERVER_ZMQ_HPP

#include <optional>

#include "context.hpp"
#include "kernel_configuration.hpp"
#include "server.hpp"

#include "datasuite.hpp"

namespace datasuite
{
    class server_zmq_impl;

    class DATASUITE_API ServerZmq : public server
    {
    public:

        ~ServerZmq() override;

        using server::notify_internal_listener;

    protected:

        ServerZmq(context& context,
            const configuration& config,
            nl::json::error_handler_t eh);

        // API for inheriting classes
        void start_publisher_thread();
        void start_heartbeat_thread();
        void stop_channels();

        void set_request_stop(bool stop);
        bool is_stopped() const;

        // The following methods must be called in the same thread
        using message_channel = std::pair<message, channel>;
        std::optional<message_channel> poll_channels(long timeout);
        void send_shell_message(message msg);
        void send_control_message(message msg);

    private:

        // Implementation of server virtual methods
        control_messenger& get_control_messenger_impl() override;

        void send_shell_impl(message msg) override;
        void send_control_impl(message msg) override;
        void send_stdin_impl(message msg) override;
        void publish_impl(pub_message msg, channel c) override;

        void abort_queue_impl(const listener& l, long polling_interval) override;
        void update_config_impl(KernelConfiguration& config) const override;

        std::unique_ptr<server_zmq_impl> p_impl;
    };

    DATASUITE_API
    std::unique_ptr<server> make_server_default(context& context,
            const configuration& config,
            nl::json::error_handler_t eh = nl::json::error_handler_t::strict);
}

#endif