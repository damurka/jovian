#include "server_zmq_default.hpp"

namespace datasuite
{
    server_zmq_default::server_zmq_default(context& context,
        const configuration& config,
        nl::json::error_handler_t eh)
        : server_zmq(context, config, eh)
    {
    }

    void server_zmq_default::start_impl(pub_message msg)
    {
        start_publisher_thread();
        start_heartbeat_thread();

        publish(std::move(msg), channel::SHELL);

        while (!is_stopped())
        {
            auto msg = poll_channels(-1);
            if (msg)
            {
                if (msg.value().second == channel::SHELL)
                {
                    notify_shell_listener(std::move(msg.value().first));
                }
                else
                {
                    notify_control_listener(std::move(msg.value().first));
                }
            }
        }

        stop_channels();
    }

    void server_zmq_default::stop_impl()
    {
        set_request_stop(true);
    }

    std::unique_ptr<server> make_server_default(context& context,
        const configuration& config,
        nl::json::error_handler_t eh)
    {
        return std::make_unique<server_zmq_default>(context, config, eh);
    }
}
