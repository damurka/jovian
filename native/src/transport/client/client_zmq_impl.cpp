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

    ClientZmqImpl::ClientZmqImpl(zmq::context_t& context,
        const KernelConfiguration& config,
        json::error_handler_t eh)
        : p_auth(make_authentication(config.m_signatureScheme, config.m_key))
        , m_shellClient(context, config.m_transport, config.m_ip, config.m_shellPort)
        , m_controlClient(context, config.m_transport, config.m_ip, config.m_controlPort)
        , m_iopubClient(context, config, this)
        , m_heartbeatClient(context, config, max_retry, heartbeat_timeout)
        , p_messenger(context)
        , m_errorHandler(eh)
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    ClientZmqImpl::~ClientZmqImpl() = default;

    void ClientZmqImpl::send_on_shell(Message msg)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(msg), *p_auth, m_errorHandler);
        m_shellClient.send_message(wire_msg);
    }

    void ClientZmqImpl::send_on_control(Message msg)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(msg), *p_auth, m_errorHandler);
        m_controlClient.send_message(wire_msg);
    }

    std::optional<Message> ClientZmqImpl::receive_on_shell(bool blocking)
    {
        std::optional<zmq::multipart_t> wire_msg = m_shellClient.receive_message(blocking);

        if (wire_msg.has_value())
        {
            return deserialize(wire_msg.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    std::optional<Message> ClientZmqImpl::receive_on_control(bool blocking)
    {
        std::optional<zmq::multipart_t> wire_msg = m_controlClient.receive_message(blocking);

        if (wire_msg.has_value())
        {
            return deserialize(wire_msg.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    void ClientZmqImpl::register_shell_listener(const listener& l)
    {
        m_shellListener = l;
    }

    void ClientZmqImpl::register_control_listener(const listener& l)
    {
        m_controlListener = l;
    }

    std::size_t ClientZmqImpl::iopub_queue_size() const
    {
        return m_iopubClient.iopub_queue_size();
    }

    std::optional<PubMessage> ClientZmqImpl::pop_iopub_message()
    {
        return m_iopubClient.pop_iopub_message();
    }

    void ClientZmqImpl::register_iopub_listener(const iopub_listener& l)
    {
        m_iopubListener = l;
    }

    void ClientZmqImpl::register_kernel_status_listener(const kernel_status_listener& l)
    {
        m_heartbeatClient.register_kernel_status_listener(l);
    }

    void ClientZmqImpl::connect()
    {
        p_messenger.connect();
    }

    void ClientZmqImpl::stop_channels()
    {
        p_messenger.stop_channels();
    }

    void ClientZmqImpl::notify_shell_listener(Message msg)
    {
        m_shellListener(std::move(msg));
    }

    void ClientZmqImpl::notify_control_listener(Message msg)
    {
        m_controlListener(std::move(msg));
    }

    void ClientZmqImpl::notify_iopub_listener(PubMessage msg)
    {
        m_iopubListener(std::move(msg));
    }

    void ClientZmqImpl::notify_kernel_dead(bool status)
    {
        m_heartbeatClient.notify_kernel_dead(status);
    }

    void ClientZmqImpl::poll(long timeout)
    {
        zmq::multipart_t wire_msg;
        zmq::pollitem_t items[]
            = { { m_shellClient.get_socket(), 0, ZMQ_POLLIN, 0 }, { m_controlClient.get_socket(), 0, ZMQ_POLLIN, 0 } };

        while (true)
        {
            zmq::poll(&items[0], 2, std::chrono::milliseconds(timeout));
            try
            {
                if (items[0].revents & ZMQ_POLLIN)
                {
                    wire_msg.recv(m_shellClient.get_socket());
                    Message msg = deserialize(wire_msg);
                    notify_shell_listener(std::move(msg));
                    return;
                }
                if (items[1].revents & ZMQ_POLLIN)
                {
                    wire_msg.recv(m_controlClient.get_socket());
                    Message msg = deserialize(wire_msg);
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

    void ClientZmqImpl::wait_for_message()
    {
        std::optional<PubMessage> pending_message = pop_iopub_message();

        if (pending_message.has_value())
        {
            notify_iopub_listener(std::move(*pending_message));
        }
        else
        {
            poll(-1);
        }
    }

    void ClientZmqImpl::start()
    {
        start_iopub_thread();
        start_heartbeat_thread();
    }

    void ClientZmqImpl::start_iopub_thread()
    {
        m_iopubThread = std::move(Thread(&ClientIopub::run, &m_iopubClient));
    }

    void ClientZmqImpl::start_heartbeat_thread()
    {
        m_heartbeatThread = std::move(Thread(&ClientHeartbeat::run, &m_heartbeatClient));
    }

    Message ClientZmqImpl::deserialize(zmq::multipart_t& wire_msg) const
    {
        return ZmqSerializer::deserialize(wire_msg, *p_auth);
    }

    PubMessage ClientZmqImpl::deserialize_iopub(zmq::multipart_t& wire_msg) const
    {
        return ZmqSerializer::deserialize_iopub(wire_msg, *p_auth);
    }

}
