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
        if (m_mainLoop)
        {
            // the language's own loop (see Server::setMainLoop()), which calls pollOnce()
            m_mainLoop();
        }
        else
        {
            while (pollOnceImpl(kIdleSliceMs))
            {
            }
        }

        if (!m_channelsStopped)
        {
            m_channelsStopped = true;
            stopChannels();
        }
    }

    bool ServerZmqDefault::pollOnceImpl(long timeoutMs)
    {
        if (!isStopped())
        {
            auto msg = pollChannels(timeoutMs);
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
        if (isStopped())
        {
            // closed now, not after the loop: with a language's own loop, the process may end as soon as this
            // returns (R exits when its REPL reads the end of its input)
            if (!m_channelsStopped)
            {
                m_channelsStopped = true;
                stopChannels();
            }
            return false;
        }
        return true;
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
