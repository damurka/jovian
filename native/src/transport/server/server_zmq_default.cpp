#include "server_zmq_default.hpp"

namespace datasuite
{
    ServerZmqDefault::ServerZmqDefault(Context& context,
        const configuration& config,
        json::error_handler_t eh)
        : ServerZmq(context, config, eh)
    {
    }

    void ServerZmqDefault::start_impl(PubMessage msg)
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

    void ServerZmqDefault::stop_impl()
    {
        set_request_stop(true);
    }

    std::unique_ptr<Server> make_server_default(Context& context,
        const configuration& config,
        json::error_handler_t eh)
    {
        return std::make_unique<ServerZmqDefault>(context, config, eh);
    }
}
