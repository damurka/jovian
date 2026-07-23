#include "server_zmq_default.hpp"

namespace datasuite
{
    ServerZmqDefault::ServerZmqDefault(Context& context,
        const configuration& config,
        json::error_handler_t eh)
        : ServerZmq(context, config, eh)
    {
    }

    void ServerZmqDefault::startImpl(PubMessage msg)
    {
        startPublisherThread();
        startHeartbeatThread();

        publish(std::move(msg), channel::SHELL);

        while (!isStopped())
        {
            auto msg = pollChannels(-1);
            if (msg)
            {
                if (msg.value().second == channel::SHELL)
                {
                    notifyShellListener(std::move(msg.value().first));
                }
                else
                {
                    notifyControlListener(std::move(msg.value().first));
                }
            }
        }

        stopChannels();
    }

    void ServerZmqDefault::stopImpl()
    {
        setRequestStop(true);
    }

    std::unique_ptr<Server> makeServerDefault(Context& context,
        const configuration& config,
        json::error_handler_t eh)
    {
        return std::make_unique<ServerZmqDefault>(context, config, eh);
    }
}
