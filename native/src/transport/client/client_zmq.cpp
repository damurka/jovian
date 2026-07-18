#include "client_zmq.hpp"
#include "client_zmq_impl.hpp"

namespace datasuite
{
    client_zmq::client_zmq(std::unique_ptr<client_zmq_impl> impl)
        : p_client_impl(std::move(impl))
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    client_zmq::~client_zmq() = default;

    void client_zmq::connect()
    {
        p_client_impl->connect();
    }

    void client_zmq::start()
    {
        p_client_impl->start();
    }

    void client_zmq::stop_channels()
    {
        p_client_impl->stop_channels();
    }

    void client_zmq::send_on_shell(message msg)
    {
        p_client_impl->send_on_shell(std::move(msg));
    }

    void client_zmq::send_on_control(message msg)
    {
        p_client_impl->send_on_control(std::move(msg));
    }

    std::optional<message> client_zmq::receive_on_shell(bool blocking)
    {
        return p_client_impl->receive_on_shell(blocking);
    }

    std::optional<message> client_zmq::receive_on_control(bool blocking)
    {
        return p_client_impl->receive_on_control(blocking);
    }

    std::size_t client_zmq::iopub_queue_size() const
    {
        return p_client_impl->iopub_queue_size();
    }

    std::optional<pub_message> client_zmq::pop_iopub_message()
    {
        return p_client_impl->pop_iopub_message();
    }

    void client_zmq::register_shell_listener(const listener& l)
    {
        p_client_impl->register_shell_listener(l);
    }

    void client_zmq::register_control_listener(const listener& l)
    {
        p_client_impl->register_control_listener(l);
    }

    void client_zmq::register_iopub_listener(const iopub_listener& l)
    {
        p_client_impl->register_iopub_listener(l);
    }

    void client_zmq::register_kernel_status_listener(const kernel_status_listener& l)
    {
        p_client_impl->register_kernel_status_listener(l);
    }

    void client_zmq::wait_for_message()
    {
        p_client_impl->wait_for_message();
    }

    std::unique_ptr<client_zmq> make_client_zmq(context& context,
        const kernel_configuration& config,
        nl::json::error_handler_t eh)
    {
        auto impl = std::make_unique<client_zmq_impl>(context.get_wrapped_context<zmq::context_t>(), config, eh);
        return std::make_unique<client_zmq>(std::move(impl));
    }
}
