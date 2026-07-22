#include "datasuite/server_zmq.hpp"
#include "handshaking.hpp"
#include "server_zmq_impl.hpp"

namespace datasuite
{
    ServerZmq::ServerZmq(Context& context,
        const configuration& config,
        json::error_handler_t eh)
        : p_impl(new ServerZmqImpl(
            context.get_wrapped_context<zmq::context_t>(),
            config,
            datasuite::get_kernel_configuration(config),
            eh,
            std::bind(&ServerZmq::notify_internal_listener, this, std::placeholders::_1)))
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    ServerZmq::~ServerZmq() = default;

    ////////////////////////////////
    // API for inheriting classes //
    ////////////////////////////////

    void ServerZmq::start_publisher_thread()
    {
        p_impl->start_publisher_thread();
    }

    void ServerZmq::start_heartbeat_thread()
    {
        p_impl->start_heartbeat_thread();
    }

    void ServerZmq::stop_channels()
    {
        p_impl->stop_channels();
    }

    void ServerZmq::set_request_stop(bool stop)
    {
        p_impl->set_request_stop(stop);
    }

    bool ServerZmq::is_stopped() const
    {
        return p_impl->is_stopped();
    }

    auto ServerZmq::poll_channels(long timeout) -> std::optional<message_channel>
    {
        return p_impl->poll_channels(timeout);
    }

    void ServerZmq::send_shell_message(Message msg)
    {
        p_impl->send_shell(std::move(msg));
    }

    void ServerZmq::send_control_message(Message msg)
    {
        p_impl->send_control(std::move(msg));
    }

    ///////////////////////////////////////////////
    // Implementation of server virtual methods //
    ///////////////////////////////////////////////

    ControlMessenger& ServerZmq::get_control_messenger_impl()
    {
        return p_impl->get_control_messenger();
    }

    void ServerZmq::send_shell_impl(Message msg)
    {
        send_shell_message(std::move(msg));
    }

    void ServerZmq::send_control_impl(Message msg)
    {
        send_control_message(std::move(msg));
    }

    void ServerZmq::send_stdin_impl(Message msg)
    {
        auto reply = p_impl->send_stdin(std::move(msg));
        if (reply)
        {
            Server::notify_stdin_listener(std::move(reply.value()));
        }
    }

    void ServerZmq::publish_impl(PubMessage msg, channel c)
    {
        p_impl->publish(std::move(msg), c);
    }

    void ServerZmq::abort_queue_impl(const listener& l, long polling_interval)
    {
        p_impl->abort_queue(l, polling_interval);
    }

    void ServerZmq::update_config_impl(KernelConfiguration& config) const
    {
        p_impl->update_config(config);
    }
}
