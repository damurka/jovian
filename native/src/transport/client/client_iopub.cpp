#include <iostream>

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
        , m_iopubEndPoint("")
        , p_clientImpl(client)
    {
        m_iopubEndPoint = getEndPoint(config.m_transport, config.m_ip, config.m_iopubPort);
        m_iopub.connect(m_iopubEndPoint);
        m_iopub.set(zmq::sockopt::subscribe, "");
        initSocket(m_controller, getControllerEndPoint("iopub"));
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

    void ClientIopub::run()
    {
        zmq::pollitem_t items[] = {
            { m_iopub, 0, ZMQ_POLLIN, 0 }, { m_controller, 0, ZMQ_POLLIN, 0 }
        };

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
                    {
                        std::lock_guard<std::mutex> guard(m_queueMutex);
                        m_messageQueue.push(std::move(msg));
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
