#include "client_zmq.hpp"
#include "client_zmq_impl.hpp"

namespace datasuite
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

    void ClientZmq::stop_channels()
    {
        p_clientImpl->stop_channels();
    }

    void ClientZmq::send_on_shell(Message msg)
    {
        p_clientImpl->send_on_shell(std::move(msg));
    }

    void ClientZmq::send_on_control(Message msg)
    {
        p_clientImpl->send_on_control(std::move(msg));
    }

    std::optional<Message> ClientZmq::receive_on_shell(bool blocking)
    {
        return p_clientImpl->receive_on_shell(blocking);
    }

    std::optional<Message> ClientZmq::receive_on_control(bool blocking)
    {
        return p_clientImpl->receive_on_control(blocking);
    }

    std::size_t ClientZmq::iopub_queue_size() const
    {
        return p_clientImpl->iopub_queue_size();
    }

    std::optional<PubMessage> ClientZmq::pop_iopub_message()
    {
        return p_clientImpl->pop_iopub_message();
    }

    void ClientZmq::register_shell_listener(const listener& l)
    {
        p_clientImpl->register_shell_listener(l);
    }

    void ClientZmq::register_control_listener(const listener& l)
    {
        p_clientImpl->register_control_listener(l);
    }

    void ClientZmq::register_iopub_listener(const iopub_listener& l)
    {
        p_clientImpl->register_iopub_listener(l);
    }

    void ClientZmq::register_kernel_status_listener(const kernel_status_listener& l)
    {
        p_clientImpl->register_kernel_status_listener(l);
    }

    void ClientZmq::wait_for_message()
    {
        p_clientImpl->wait_for_message();
    }

    std::unique_ptr<ClientZmq> make_client_zmq(Context& context,
        const KernelConfiguration& config,
        json::error_handler_t eh)
    {
        auto impl = std::make_unique<ClientZmqImpl>(context.get_wrapped_context<zmq::context_t>(), config, eh);
        return std::make_unique<ClientZmq>(std::move(impl));
    }
}
