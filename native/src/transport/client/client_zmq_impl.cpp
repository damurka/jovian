#include <iostream>

#include "client_zmq_impl.hpp"
#include "../common/authentication.hpp"
#include "../common/zmq_serializer.hpp"

namespace datasuite
{
    namespace
    {
        constexpr std::size_t max_retry = 3;
        constexpr long heartbeat_timeout = std::chrono::milliseconds(20000).count();
    }

    client_zmq_impl::client_zmq_impl(zmq::context_t& context,
        const kernel_configuration& config,
        nl::json::error_handler_t eh)
        : p_auth(make_authentication(config.m_signature_scheme, config.m_key))
        , m_shell_client(context, config.m_transport, config.m_ip, config.m_shell_port)
        , m_control_client(context, config.m_transport, config.m_ip, config.m_control_port)
        , m_iopub_client(context, config, this)
        , m_heartbeat_client(context, config, max_retry, heartbeat_timeout)
        , p_messenger(context)
        , m_error_handler(eh)
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    client_zmq_impl::~client_zmq_impl() = default;

    void client_zmq_impl::send_on_shell(message msg)
    {
        zmq::multipart_t wire_msg = zmq_serializer::serialize(std::move(msg), *p_auth, m_error_handler);
        m_shell_client.send_message(wire_msg);
    }

    void client_zmq_impl::send_on_control(message msg)
    {
        zmq::multipart_t wire_msg = zmq_serializer::serialize(std::move(msg), *p_auth, m_error_handler);
        m_control_client.send_message(wire_msg);
    }

    std::optional<message> client_zmq_impl::receive_on_shell(bool blocking)
    {
        std::optional<zmq::multipart_t> wire_msg = m_shell_client.receive_message(blocking);

        if (wire_msg.has_value())
        {
            return deserialize(wire_msg.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    std::optional<message> client_zmq_impl::receive_on_control(bool blocking)
    {
        std::optional<zmq::multipart_t> wire_msg = m_control_client.receive_message(blocking);

        if (wire_msg.has_value())
        {
            return deserialize(wire_msg.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    void client_zmq_impl::register_shell_listener(const listener& l)
    {
        m_shell_listener = l;
    }

    void client_zmq_impl::register_control_listener(const listener& l)
    {
        m_control_listener = l;
    }

    std::size_t client_zmq_impl::iopub_queue_size() const
    {
        return m_iopub_client.iopub_queue_size();
    }

    std::optional<pub_message> client_zmq_impl::pop_iopub_message()
    {
        return m_iopub_client.pop_iopub_message();
    }

    void client_zmq_impl::register_iopub_listener(const iopub_listener& l)
    {
        m_iopub_listener = l;
    }

    void client_zmq_impl::register_kernel_status_listener(const kernel_status_listener& l)
    {
        m_heartbeat_client.register_kernel_status_listener(l);
    }

    void client_zmq_impl::connect()
    {
        p_messenger.connect();
    }

    void client_zmq_impl::stop_channels()
    {
        p_messenger.stop_channels();
    }

    void client_zmq_impl::notify_shell_listener(message msg)
    {
        m_shell_listener(std::move(msg));
    }

    void client_zmq_impl::notify_control_listener(message msg)
    {
        m_control_listener(std::move(msg));
    }

    void client_zmq_impl::notify_iopub_listener(pub_message msg)
    {
        m_iopub_listener(std::move(msg));
    }

    void client_zmq_impl::notify_kernel_dead(bool status)
    {
        m_heartbeat_client.notify_kernel_dead(status);
    }

    void client_zmq_impl::poll(long timeout)
    {
        zmq::multipart_t wire_msg;
        zmq::pollitem_t items[]
            = { { m_shell_client.get_socket(), 0, ZMQ_POLLIN, 0 }, { m_control_client.get_socket(), 0, ZMQ_POLLIN, 0 } };

        while (true)
        {
            zmq::poll(&items[0], 2, std::chrono::milliseconds(timeout));
            try
            {
                if (items[0].revents & ZMQ_POLLIN)
                {
                    wire_msg.recv(m_shell_client.get_socket());
                    message msg = deserialize(wire_msg);
                    notify_shell_listener(std::move(msg));
                    return;
                }
                if (items[1].revents & ZMQ_POLLIN)
                {
                    wire_msg.recv(m_control_client.get_socket());
                    message msg = deserialize(wire_msg);
                    notify_control_listener(std::move(msg));
                    return;
                }
            }
            catch (std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
        }
    }

    void client_zmq_impl::wait_for_message()
    {
        std::optional<pub_message> pending_message = pop_iopub_message();

        if (pending_message.has_value())
        {
            notify_iopub_listener(std::move(*pending_message));
        }
        else
        {
            poll(-1);
        }
    }

    void client_zmq_impl::start()
    {
        start_iopub_thread();
        start_heartbeat_thread();
    }

    void client_zmq_impl::start_iopub_thread()
    {
        m_iopub_thread = std::move(thread(&client_iopub::run, &m_iopub_client));
    }

    void client_zmq_impl::start_heartbeat_thread()
    {
        m_heartbeat_thread = std::move(thread(&client_heartbeat::run, &m_heartbeat_client));
    }

    message client_zmq_impl::deserialize(zmq::multipart_t& wire_msg) const
    {
        return zmq_serializer::deserialize(wire_msg, *p_auth);
    }

    pub_message client_zmq_impl::deserialize_iopub(zmq::multipart_t& wire_msg) const
    {
        return zmq_serializer::deserialize_iopub(wire_msg, *p_auth);
    }

}
