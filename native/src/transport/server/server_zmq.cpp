#include "datasuite/server_zmq.hpp"
#include "handshaking.hpp"
#include "server_zmq_impl.hpp"

namespace datasuite
{
    ServerZmq::ServerZmq(Context& context,
        const configuration& config,
        json::error_handler_t eh)
        : p_impl(new ServerZmqImpl(
            context.getWrappedContext<zmq::context_t>(),
            config,
            datasuite::getKernelConfiguration(config),
            eh,
            std::bind(&ServerZmq::notifyInternalListener, this, std::placeholders::_1)))
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    ServerZmq::~ServerZmq() = default;

    ////////////////////////////////
    // API for inheriting classes //
    ////////////////////////////////

    void ServerZmq::startPublisherThread()
    {
        p_impl->startPublisherThread();
    }

    void ServerZmq::startHeartbeatThread()
    {
        p_impl->startHeartbeatThread();
    }

    void ServerZmq::stopChannels()
    {
        p_impl->stopChannels();
    }

    void ServerZmq::setRequestStop(bool stop)
    {
        p_impl->setRequestStop(stop);
    }

    bool ServerZmq::isStopped() const
    {
        return p_impl->isStopped();
    }

    auto ServerZmq::pollChannels(long timeout) -> std::optional<message_channel>
    {
        return p_impl->pollChannels(timeout);
    }

    void ServerZmq::sendShellMessage(Message msg)
    {
        p_impl->sendShell(std::move(msg));
    }

    void ServerZmq::sendControlMessage(Message msg)
    {
        p_impl->sendControl(std::move(msg));
    }

    ///////////////////////////////////////////////
    // Implementation of server virtual methods //
    ///////////////////////////////////////////////

    ControlMessenger& ServerZmq::getControlMessengerImpl()
    {
        return p_impl->getControlMessenger();
    }

    void ServerZmq::sendShellImpl(Message msg)
    {
        sendShellMessage(std::move(msg));
    }

    void ServerZmq::sendControlImpl(Message msg)
    {
        sendControlMessage(std::move(msg));
    }

    void ServerZmq::sendStdinImpl(Message msg)
    {
        auto reply = p_impl->sendStdin(std::move(msg));
        if (reply)
        {
            Server::notifyStdinListener(std::move(reply.value()));
        }
    }

    void ServerZmq::publishImpl(PubMessage msg, channel c)
    {
        p_impl->publish(std::move(msg), c);
    }

    void ServerZmq::abortQueueImpl(const listener& l, long polling_interval)
    {
        p_impl->abortQueue(l, polling_interval);
    }

    void ServerZmq::updateConfigImpl(KernelConfiguration& config) const
    {
        p_impl->updateConfig(config);
    }
}
