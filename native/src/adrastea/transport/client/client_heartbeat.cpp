#include <iostream>

#include "client_heartbeat.hpp"
#include "client_zmq_impl.hpp"
#include "../common/middleware_impl.hpp"

namespace adrastea
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
        // Default ZMQ_LINGER is -1 (block on close until every queued
        // message is delivered). m_controller gets a bounded linger via
        // initSocket() below, but this socket never did -- normally masked
        // in production because a live kernel's heartbeat port actually
        // accepts the connection, so pings never sit undelivered. A kernel
        // that's dead/never started (nothing listening) leaves the last
        // ping queued forever, and closing this socket -- or the owning
        // zmq::context_t -- then blocks indefinitely instead of tearing
        // down.
        m_heartbeat.set(zmq::sockopt::linger, 0);

        m_heartbeatEndPoint = getEndPoint(config.m_transport, config.m_ip, config.m_hbPort);
        m_heartbeat.connect(m_heartbeatEndPoint);
        initSocket(m_controller, getControllerEndPoint("heartbeat"));
    }

    ClientHeartbeat::~ClientHeartbeat()
    {
        m_heartbeat.disconnect(m_heartbeatEndPoint);
    }

    void ClientHeartbeat::sendHeartbeatMessage()
    {
        zmq::message_t ping_msg("ping", 4);
        m_heartbeat.send(ping_msg, zmq::send_flags::none);
    }

    bool ClientHeartbeat::waitForAnswer(long timeout)
    {
        zmq::pollitem_t items[] = {
            { m_heartbeat, 0, ZMQ_POLLIN, 0 }, { m_controller, 0, ZMQ_POLLIN, 0 }
        };

        zmq::poll(&items[0], 2, std::chrono::milliseconds(timeout));
        // Must reflect whether a pong actually arrived, not just whether
        // poll() returned without throwing -- run()'s retry/dead-kernel
        // logic below treats `false` as "no answer this round", and a plain
        // timeout (nothing in items[0].revents, no exception) is exactly
        // that case. Returning unconditional `true` here made
        // notifyKernelDead() unreachable from a real missed heartbeat: the
        // only way run() ever saw `false` was a genuine ZMQ exception.
        bool gotAnswer = false;
        try
        {
            if (items[0].revents & ZMQ_POLLIN)
            {
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_heartbeat);
                gotAnswer = true;
            }

            if (items[1].revents & ZMQ_POLLIN)
            {
                // stop message
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_controller);
                wire_msg.send(m_controller);
                m_requestStop = true;
            }

            return gotAnswer;
        }
        catch (std::exception& e)
        {
            std::cerr << e.what() << std::endl;
        }
        return false;
    }

    void ClientHeartbeat::registerKernelStatusListener(const kernel_status_listener& l)
    {
        m_kernelStatusListener = l;
    }

    void ClientHeartbeat::notifyKernelDead(bool status)
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
                sendHeartbeatMessage();
                if (!waitForAnswer(m_heartbeatTimeout))
                {
                    if (retry_count < m_maxRetry)
                    {
                        ++retry_count;
                    }
                    else
                    {
                        notifyKernelDead(true);
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
