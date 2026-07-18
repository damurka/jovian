#include "datasuite/server_zmq.hpp"
#include "handshaking.hpp"
#include "server_zmq_impl.hpp"

namespace datasuite
{
    server_zmq::server_zmq(context& context,
        const configuration& config,
        nl::json::error_handler_t eh)
        : p_impl(new server_zmq_impl(
            context.get_wrapped_context<zmq::context_t>(),
            config,
            datasuite::get_kernel_configuration(config),
            eh,
            std::bind(&server_zmq::notify_internal_listener, this, std::placeholders::_1)))
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    server_zmq::~server_zmq() = default;

    ////////////////////////////////
    // API for inheriting classes //
    ////////////////////////////////

    void server_zmq::start_publisher_thread()
    {
        p_impl->start_publisher_thread();
    }

    void server_zmq::start_heartbeat_thread()
    {
        p_impl->start_heartbeat_thread();
    }

    void server_zmq::stop_channels()
    {
        p_impl->stop_channels();
    }

    void server_zmq::set_request_stop(bool stop)
    {
        p_impl->set_request_stop(stop);
    }

    bool server_zmq::is_stopped() const
    {
        return p_impl->is_stopped();
    }

    auto server_zmq::poll_channels(long timeout) -> std::optional<message_channel>
    {
        return p_impl->poll_channels(timeout);
    }

    void server_zmq::send_shell_message(message msg)
    {
        p_impl->send_shell(std::move(msg));
    }

    void server_zmq::send_control_message(message msg)
    {
        p_impl->send_control(std::move(msg));
    }

    ///////////////////////////////////////////////
    // Implementation of server virtual methods //
    ///////////////////////////////////////////////

    control_messenger& server_zmq::get_control_messenger_impl()
    {
        return p_impl->get_control_messenger();
    }

    void server_zmq::send_shell_impl(message msg)
    {
        send_shell_message(std::move(msg));
    }

    void server_zmq::send_control_impl(message msg)
    {
        send_control_message(std::move(msg));
    }

    void server_zmq::send_stdin_impl(message msg)
    {
        auto reply = p_impl->send_stdin(std::move(msg));
        if (reply)
        {
            server::notify_stdin_listener(std::move(reply.value()));
        }
    }

    void server_zmq::publish_impl(pub_message msg, channel c)
    {
        p_impl->publish(std::move(msg), c);
    }

    void server_zmq::abort_queue_impl(const listener& l, long polling_interval)
    {
        p_impl->abort_queue(l, polling_interval);
    }

    void server_zmq::update_config_impl(kernel_configuration& config) const
    {
        p_impl->update_config(config);
    }
}
