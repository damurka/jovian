#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <string>

#include "client_iopub.hpp"
#include "client_zmq_impl.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace adrastea
{

    ClientIopub::ClientIopub(zmq::context_t& context,
        const KernelConfiguration& config,
        ClientZmqImpl* client)
        : m_iopub(context, zmq::socket_type::sub)
        , m_controller(context, zmq::socket_type::rep)
        , m_signalSend(context, zmq::socket_type::pair)
        , m_signalReceive(context, zmq::socket_type::pair)
        , m_iopubEndPoint("")
        , p_clientImpl(client)
    {
        m_iopubEndPoint = getEndPoint(config.m_transport, config.m_ip, config.m_iopubPort);
        // Never drop output (see server/publisher.cpp).
        m_iopub.set(zmq::sockopt::rcvhwm, 0);
        m_iopub.connect(m_iopubEndPoint);
        m_iopub.set(zmq::sockopt::subscribe, "");
        initSocket(m_controller, getControllerEndPoint("iopub"));
        const std::string signalEndPoint = "inproc://adrastea-iopub-queued-" + std::to_string(reinterpret_cast<std::uintptr_t>(this));
        m_signalReceive.bind(signalEndPoint);
        m_signalSend.connect(signalEndPoint);
    }

    zmq::socket_t& ClientIopub::queuedSignal()
    {
        return m_signalReceive;
    }

    void ClientIopub::clearQueuedSignal()
    {
        // cleared before the queue is drained: a message queued from now on signals again
        m_signalled.store(false);
        zmq::message_t signal;
        while (m_signalReceive.recv(signal, zmq::recv_flags::dontwait))
        {
        }
    }

    ClientIopub::~ClientIopub()
    {
        m_iopub.disconnect(m_iopubEndPoint);
    }

    std::size_t ClientIopub::iopubQueueSize() const
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        return m_messageQueue.size();
    }

    std::optional<PubMessage> ClientIopub::popIopubMessage()
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        if (!m_messageQueue.empty())
        {
            PubMessage msg = std::move(m_messageQueue.front());
            m_messageQueue.pop();
            return msg;
        }
        else
        {
            return std::nullopt;
        }
    }

    void ClientIopub::waitUntilListening()
    {
        std::unique_lock<std::mutex> lock(m_stateMutex);
        m_stateChanged.wait(lock, [this] { return m_listening; });
    }

    bool ClientIopub::waitForWelcome(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(m_stateMutex);
        return m_stateChanged.wait_for(lock, timeout, [this] { return m_welcomed; });
    }

    void ClientIopub::run()
    {
        zmq::pollitem_t items[] = {
            { m_iopub, 0, ZMQ_POLLIN, 0 }, { m_controller, 0, ZMQ_POLLIN, 0 }
        };
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_listening = true;
            m_stateChanged.notify_all();
        }

        while (true)
        {
            zmq::poll(&items[0], 2, std::chrono::milliseconds(-1));
            try
            {
                if (items[0].revents & ZMQ_POLLIN)
                {
                    zmq::multipart_t wire_msg;
                    wire_msg.recv(m_iopub);
                    PubMessage msg = p_clientImpl->deserializeIopub(wire_msg);
                    if (msg.header().value("msg_type", "") == "iopub_welcome")
                    {
                        std::lock_guard<std::mutex> lock(m_stateMutex);
                        m_welcomed = true;
                        m_stateChanged.notify_all();
                    }
                    {
                        std::lock_guard<std::mutex> guard(m_queueMutex);
                        m_messageQueue.push(std::move(msg));
                    }
                    // one signal however many messages are queued before the waiter wakes
                    if (!m_signalled.exchange(true))
                    {
                        m_signalSend.send(zmq::message_t(), zmq::send_flags::dontwait);
                    }
                }
                if (items[1].revents & ZMQ_POLLIN)
                {
                    // stop message
                    zmq::multipart_t wire_msg;
                    wire_msg.recv(m_controller);
                    wire_msg.send(m_controller);
                    break;
                }
            }
            catch (std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
        }
    }
}
