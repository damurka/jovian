#include <iostream>

#include "handshaking.hpp"
#include "server_zmq_impl.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace datasuite
{
    server_zmq_impl::server_zmq_impl(zmq::context_t& context,
        const configuration& initial_config,
        KernelConfiguration kernel_config,
        nl::json::error_handler_t eh,
        internal_listener listener)
        : m_shell(context, zmq::socket_type::router)
        , m_controller(context, zmq::socket_type::router)
        , m_stdin(context, zmq::socket_type::router)
        , m_publisherPub(context, zmq::socket_type::pub)
        , m_publisherController(context, zmq::socket_type::req)
        , m_heartbeatController(context, zmq::socket_type::req)
        , p_auth(make_authentication(kernel_config.m_signatureScheme, kernel_config.m_key))
        , m_publisher(context,
            std::bind(&server_zmq_impl::serialize_iopub, this, std::placeholders::_1),
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

        if (std::holds_alternative<registration_configuration>(initial_config))
        {
            update_config(kernel_config);
            datasuite::send_connection_info
            (
                context,
                std::get<registration_configuration>(initial_config),
                kernel_config,
                *p_auth,
                m_errorHandler
            );
        }
    }

    void server_zmq_impl::start_publisher_thread()
    {
        m_iopubThread = thread(&publisher::run, &m_publisher);
    }

    void server_zmq_impl::start_heartbeat_thread()
    {
        m_hbThread = thread(&heartbeat::run, &m_heartbeat);
    }

    void server_zmq_impl::stop_channels()
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

    void server_zmq_impl::set_request_stop(bool stop)
    {
        m_requestStop = stop;
    }

    bool server_zmq_impl::is_stopped() const
    {
        return m_requestStop;
    }

    auto server_zmq_impl::poll_channels(long timeout) -> std::optional<message_channel>
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
                message msg = zmq_serializer::deserialize(wire_msg, *p_auth);
                return { std::make_pair(std::move(msg), channel::CONTROL) };
            }

            if (!m_requestStop && (items[1].revents & ZMQ_POLLIN))
            {
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_shell);
                message msg = zmq_serializer::deserialize(wire_msg, *p_auth);
                return { std::make_pair(std::move(msg), channel::SHELL) };
            }
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }

        return std::nullopt;
    }

    control_messenger& server_zmq_impl::get_control_messenger()
    {
        return m_messenger;
    }

    void server_zmq_impl::send_shell(message message)
    {
        zmq::multipart_t wire_msg = zmq_serializer::serialize(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_shell);
    }

    void server_zmq_impl::send_control(message message)
    {
        zmq::multipart_t wire_msg = zmq_serializer::serialize(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_controller);
    }

    std::optional<message> server_zmq_impl::send_stdin(message message)
    {
        zmq::multipart_t wire_msg = zmq_serializer::serialize(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_stdin);
        zmq::multipart_t wire_reply;
        // Block until a response to the input request is received.
        wire_reply.recv(m_stdin);
        try
        {
            return zmq_serializer::deserialize(wire_reply, *p_auth);
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return std::nullopt;
    }

    void server_zmq_impl::publish(pub_message message, channel)
    {
        zmq::multipart_t wire_msg = zmq_serializer::serialize_iopub(std::move(message), *p_auth, m_errorHandler);
        wire_msg.send(m_publisherPub);
    }

    void server_zmq_impl::abort_queue(const listener& l, long polling_interval)
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
                message msg = zmq_serializer::deserialize(wire_msg, *p_auth);
                l(std::move(msg));
            }
            catch (std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(polling_interval));
        }
    }

    void server_zmq_impl::update_config(KernelConfiguration& config) const
    {
        config.m_controlPort = get_socket_port(m_controller);
        config.m_shellPort = get_socket_port(m_shell);
        config.m_stdinPort = get_socket_port(m_stdin);
        config.m_iopubPort = m_publisher.get_port();
        config.m_hbPort = m_heartbeat.get_port();
    }

    zmq::multipart_t server_zmq_impl::serialize_iopub(pub_message&& msg)
    {
        return zmq_serializer::serialize_iopub(std::move(msg), *p_auth, m_errorHandler);
    }
}
