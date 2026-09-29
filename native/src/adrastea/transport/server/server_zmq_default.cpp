#include "server_zmq_default.hpp"

namespace adrastea
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

        // Short polls rather than one that blocks until a request comes: in
        // between, the interpreter runs its own idle work (R's later event
        // loop -- see Interpreter::idle()), and a reply the busy-shell thread
        // is sending gets the shell socket (see ServerZmqImpl::pollChannels()).
        constexpr long kIdleSliceMs = 20;
        while (!isStopped())
        {
            auto msg = pollChannels(kIdleSliceMs);
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
            else if (!isStopped())
            {
                notifyIdle();
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
