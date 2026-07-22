#ifndef DATASUITE_SERVER_ZMQ_HPP
#define DATASUITE_SERVER_ZMQ_HPP

#include <optional>

#include "context.hpp"
#include "kernel_configuration.hpp"
#include "server.hpp"

#include "datasuite.hpp"

namespace datasuite
{
    class ServerZmqImpl;

    class DATASUITE_API ServerZmq : public Server
    {
    public:

        ~ServerZmq() override;

        using Server::notify_internal_listener;

    protected:

        ServerZmq(Context& context,
            const configuration& config,
            json::error_handler_t eh);

        // API for inheriting classes
        void start_publisher_thread();
        void start_heartbeat_thread();
        void stop_channels();

        void set_request_stop(bool stop);
        bool is_stopped() const;

        // The following methods must be called in the same thread
        using message_channel = std::pair<Message, channel>;
        std::optional<message_channel> poll_channels(long timeout);
        void send_shell_message(Message msg);
        void send_control_message(Message msg);

    private:

        // Implementation of server virtual methods
        ControlMessenger& get_control_messenger_impl() override;

        void send_shell_impl(Message msg) override;
        void send_control_impl(Message msg) override;
        void send_stdin_impl(Message msg) override;
        void publish_impl(PubMessage msg, channel c) override;

        void abort_queue_impl(const listener& l, long polling_interval) override;
        void update_config_impl(KernelConfiguration& config) const override;

        std::unique_ptr<ServerZmqImpl> p_impl;
    };

    DATASUITE_API
    std::unique_ptr<Server> make_server_default(Context& context,
            const configuration& config,
            json::error_handler_t eh = json::error_handler_t::strict);
}

#endif
