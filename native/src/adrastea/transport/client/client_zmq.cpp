#include "client_zmq.hpp"
#include "client_zmq_impl.hpp"

namespace adrastea
{
    ClientZmq::ClientZmq(std::unique_ptr<ClientZmqImpl> impl)
        : p_clientImpl(std::move(impl))
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    ClientZmq::~ClientZmq() = default;

    void ClientZmq::connect()
    {
        p_clientImpl->connect();
    }

    void ClientZmq::start()
    {
        p_clientImpl->start();
    }

    void ClientZmq::stopChannels()
    {
        p_clientImpl->stopChannels();
    }

    void ClientZmq::sendOnShell(Message msg)
    {
        p_clientImpl->sendOnShell(std::move(msg));
    }

    void ClientZmq::sendOnControl(Message msg)
    {
        p_clientImpl->sendOnControl(std::move(msg));
    }

    std::optional<Message> ClientZmq::receiveOnShell(bool blocking)
    {
        return p_clientImpl->receiveOnShell(blocking);
    }

    std::optional<Message> ClientZmq::receiveOnControl(bool blocking)
    {
        return p_clientImpl->receiveOnControl(blocking);
    }

    void ClientZmq::sendOnStdin(Message msg)
    {
        p_clientImpl->sendOnStdin(std::move(msg));
    }

    std::optional<Message> ClientZmq::receiveOnStdin(bool blocking)
    {
        return p_clientImpl->receiveOnStdin(blocking);
    }

    std::size_t ClientZmq::iopubQueueSize() const
    {
        return p_clientImpl->iopubQueueSize();
    }

    std::optional<PubMessage> ClientZmq::popIopubMessage()
    {
        return p_clientImpl->popIopubMessage();
    }

    void ClientZmq::registerShellListener(const listener& l)
    {
        p_clientImpl->registerShellListener(l);
    }

    void ClientZmq::registerControlListener(const listener& l)
    {
        p_clientImpl->registerControlListener(l);
    }

    void ClientZmq::registerStdinListener(const listener& l)
    {
        p_clientImpl->registerStdinListener(l);
    }

    void ClientZmq::registerIopubListener(const iopub_listener& l)
    {
        p_clientImpl->registerIopubListener(l);
    }

    void ClientZmq::registerKernelStatusListener(const kernel_status_listener& l)
    {
        p_clientImpl->registerKernelStatusListener(l);
    }

    void ClientZmq::waitForMessage()
    {
        p_clientImpl->waitForMessage();
    }

    std::unique_ptr<ClientZmq> makeClientZmq(Context& context,
        const KernelConfiguration& config,
        json::error_handler_t eh)
    {
        auto impl = std::make_unique<ClientZmqImpl>(context.getWrappedContext<zmq::context_t>(), config, eh);
        return std::make_unique<ClientZmq>(std::move(impl));
    }
}
