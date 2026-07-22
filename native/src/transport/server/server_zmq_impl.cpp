#include <iostream>

#include "handshaking.hpp"
#include "server_zmq_impl.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace datasuite
{
    ServerZmqImpl::ServerZmqImpl(zmq::context_t& context,
        const configuration& initial_config,
        KernelConfiguration kernel_config,
        json::error_handler_t eh,
        internal_listener listener)
        : m_shell(context, zmq::socket_type::router)
        , m_controller(context, zmq::socket_type::router)
        , m_stdin(context, zmq::socket_type::router)
        , m_publisherPub(context, zmq::socket_type::pub)
        , m_publisherController(context, zmq::socket_type::req)
        , m_heartbeatController(context, zmq::socket_type::req)
        , p_auth(make_authentication(kernel_config.m_signatureScheme, kernel_config.m_key))
        , m_publisher(context,
            std::bind(&ServerZmqImpl::serialize_iopub, this, std::placeholders::_1),
            kernel_config.m_transport, kernel_config.m_ip, kernel_config.m_iopubPort)
        , m_heartbeat(context, kernel_config.m_transport, kernel_config.m_ip, kernel_config.m_hbPort)
        , m_iopubThread()
        , m_hbThread()
        , m_messenger(std::move(listener))
        , m_errorHandler(eh)
        , m_requestStop(false)
    {
        init_socket(m_shell, kernel_config.m_transport, kernel_config.m_ip, kernel_config.m_shellPort);
        init_socket(m_controller, kernel_config.m_transport, kernel_config.m_ip, kernel_config.m_controlPort);
        init_socket(m_stdin, kernel_config.m_transport, kernel_config.m_ip, kernel_config.m_stdinPort);
        m_publisherPub.set(zmq::sockopt::linger, get_socket_linger());
        m_publisherPub.connect(get_publisher_end_point());

        m_publisherController.set(zmq::sockopt::linger, get_socket_linger());
        m_publisherController.connect(get_controller_end_point("publisher"));
        m_heartbeatController.set(zmq::sockopt::linger, get_socket_linger());
        m_heartbeatController.connect(get_controller_end_point("heartbeat"));

        if (std::holds_alternative<RegistrationConfiguration>(initial_config))
        {
            update_config(kernel_config);
            datasuite::send_connection_info
            (
                context,
                std::get<RegistrationConfiguration>(initial_config),
                kernel_config,
                *p_auth,
                m_errorHandler
            );
        }
    }

    void ServerZmqImpl::start_publisher_thread()
    {
        m_iopubThread = Thread(&Publisher::run, &m_publisher);
    }

    void ServerZmqImpl::start_heartbeat_thread()
    {
        m_hbThread = Thread(&Heartbeat::run, &m_heartbeat);
    }

    void ServerZmqImpl::stop_channels()
    {
        zmq::message_t stop_msg("stop", 4);
        zmq::message_t response;

        // Wait for publisher answer
        m_publisherController.send(stop_msg, zmq::send_flags::none);
        (void)m_publisherController.recv(response);

        // Wait for heartbeat answer
        m_heartbeatController.send(stop_msg, zmq::send_flags::none);
        (void)m_heartbeatController.recv(response);
    }

    void ServerZmqImpl::set_request_stop(bool stop)
    {
        m_requestStop = stop;
    }

    bool ServerZmqImpl::is_stopped() const
    {
        return m_requestStop;
    }

    auto ServerZmqImpl::poll_channels(long timeout) -> std::optional<message_channel>
    {
        zmq::pollitem_t items[]
            = { { m_controller, 0, ZMQ_POLLIN, 0 }, { m_shell, 0, ZMQ_POLLIN, 0 } };

        zmq::poll(&items[0], 2, std::chrono::milliseconds(timeout));

        try
        {
            if (items[0].revents & ZMQ_POLLIN)
            {
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_controller);
                Message msg = ZmqSerializer::deserialize(wire_msg, *p_auth);
                return { std::make_pair(std::move(msg), channel::CONTROL) };
            }

            if (!m_requestStop && (items[1].revents & ZMQ_POLLIN))
            {
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_shell);
                Message msg = ZmqSerializer::deserialize(wire_msg, *p_auth);
                return { std::make_pair(std::move(msg), channel::SHELL) };
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }

        return std::nullopt;
    }

    ControlMessenger& ServerZmqImpl::get_control_messenger()
    {
        return m_messenger;
    }

    void ServerZmqImpl::send_shell(Message message)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_shell);
    }

    void ServerZmqImpl::send_control(Message message)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_controller);
    }

    std::optional<Message> ServerZmqImpl::send_stdin(Message message)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_stdin);
        zmq::multipart_t wire_reply;
        // Block until a response to the input request is received.
        wire_reply.recv(m_stdin);
        try
        {
            return ZmqSerializer::deserialize(wire_reply, *p_auth);
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return std::nullopt;
    }

    void ServerZmqImpl::publish(PubMessage message, channel)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize_iopub(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_publisherPub);
    }

    void ServerZmqImpl::abort_queue(const listener& l, long polling_interval)
    {
        while (true)
        {
            zmq::multipart_t wire_msg;
            bool msg = wire_msg.recv(m_shell, ZMQ_NOBLOCK);
            if (!msg)
            {
                return;
            }

            try
            {
                Message msg = ZmqSerializer::deserialize(wire_msg, *p_auth);
                l(std::move(msg));
            }
            catch (std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval));
        }
    }

    void ServerZmqImpl::update_config(KernelConfiguration& config) const
    {
        config.m_controlPort = get_socket_port(m_controller);
        config.m_shellPort = get_socket_port(m_shell);
        config.m_stdinPort = get_socket_port(m_stdin);
        config.m_iopubPort = m_publisher.get_port();
        config.m_hbPort = m_heartbeat.get_port();
    }

    zmq::multipart_t ServerZmqImpl::serialize_iopub(PubMessage&& msg)
    {
        return ZmqSerializer::serialize_iopub(std::move(msg), *p_auth, m_errorHandler);
    }
}
