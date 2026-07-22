#include <iostream>

#include "client_iopub.hpp"
#include "client_zmq_impl.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace datasuite
{

    client_iopub::client_iopub(zmq::context_t& context,
        const KernelConfiguration& config,
        client_zmq_impl* client)
        : m_iopub(context, zmq::socket_type::sub)
        , m_controller(context, zmq::socket_type::rep)
        , m_iopubEndPoint("")
        , p_clientImpl(client)
    {
        m_iopubEndPoint = get_end_point(config.m_transport, config.m_ip, config.m_iopubPort);
        m_iopub.connect(m_iopubEndPoint);
        m_iopub.set(zmq::sockopt::subscribe, "");
        init_socket(m_controller, get_controller_end_point("iopub"));
    }

    client_iopub::~client_iopub()
    {
        m_iopub.disconnect(m_iopubEndPoint);
    }

    std::size_t client_iopub::iopub_queue_size() const
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        return m_messageQueue.size();
    }

    std::optional<pub_message> client_iopub::pop_iopub_message()
    {
        std::lock_guard<std::mutex> guard(m_queueMutex);
        if (!m_messageQueue.empty())
        {
            pub_message msg = std::move(m_messageQueue.front());
            m_messageQueue.pop();
            return msg;
        }
        else
        {
            return std::nullopt;
        }
    }

    void client_iopub::run()
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
                    pub_message msg = p_clientImpl->deserialize_iopub(wire_msg);
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
