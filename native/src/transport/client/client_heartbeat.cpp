#include <iostream>

#include "client_heartbeat.hpp"
#include "client_zmq_impl.hpp"
#include "../common/middleware_impl.hpp"

namespace datasuite
{

    ClientHeartbeat::ClientHeartbeat(zmq::context_t& context,
        const KernelConfiguration& config,
        const std::size_t max_retry,
        const long timeout)
        : m_heartbeat(context, zmq::socket_type::req)
        , m_controller(context, zmq::socket_type::rep)
        , m_maxRetry(max_retry)
        , m_heartbeatTimeout(timeout)
        , m_heartbeatEndPoint("")
        , m_requestStop(false)
    {
        m_heartbeat.set(zmq::sockopt::req_relaxed, 1);
        m_heartbeat.set(zmq::sockopt::req_correlate, 1);

        m_heartbeatEndPoint = get_end_point(config.m_transport, config.m_ip, config.m_hbPort);
        m_heartbeat.connect(m_heartbeatEndPoint);
        init_socket(m_controller, get_controller_end_point("heartbeat"));
    }

    ClientHeartbeat::~ClientHeartbeat()
    {
        m_heartbeat.disconnect(m_heartbeatEndPoint);
    }

    void ClientHeartbeat::send_heartbeat_message()
    {
        zmq::message_t ping_msg("ping", 4);
        m_heartbeat.send(ping_msg, zmq::send_flags::none);
    }

    bool ClientHeartbeat::wait_for_answer(long timeout)
    {
        zmq::pollitem_t items[] = {
            { m_heartbeat, 0, ZMQ_POLLIN, 0 }, { m_controller, 0, ZMQ_POLLIN, 0 }
        };

        zmq::poll(&items[0], 2, std::chrono::milliseconds(timeout));
        try
        {
            if (items[0].revents & ZMQ_POLLIN)
            {
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_heartbeat);
            }

            if (items[1].revents & ZMQ_POLLIN)
            {
                // stop message
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_controller);
                wire_msg.send(m_controller);
                m_requestStop = true;
            }

            return true;
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return false;
    }

    void ClientHeartbeat::register_kernel_status_listener(const kernel_status_listener& l)
    {
        m_kernelStatusListener = l;
    }

    void ClientHeartbeat::notify_kernel_dead(bool status)
    {
        m_kernelStatusListener(status);
    }

    void ClientHeartbeat::run()
    {
        std::size_t retry_count = 0;

        while (!m_requestStop)
        {
            try
            {
                send_heartbeat_message();
                if (!wait_for_answer(m_heartbeatTimeout))
                {
                    if (retry_count < m_maxRetry)
                    {
                        ++retry_count;
                    }
                    else
                    {
                        notify_kernel_dead(true);
                        break;
                    }
                }
                else
                {
                    retry_count = 0;
                }
            }
            catch (std::exception& e)
            {
                std::cerr << "[Heartbeat Error]: "<< e.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
