#include <iterator>
#include <string>

#include "zmq_addon.hpp"
#include "../common/middleware_impl.hpp"
#include "heartbeat.hpp"

namespace datasuite
{
    Heartbeat::Heartbeat(zmq::context_t& context,
        const std::string& transport,
        const std::string& ip,
        const std::string& port)
        : m_heartbeat(context, zmq::socket_type::router)
        , m_controller(context, zmq::socket_type::rep)
    {
        init_socket(m_heartbeat, transport, ip, port);
        init_socket(m_controller, get_controller_end_point("heartbeat"));
    }

    Heartbeat::~Heartbeat()
    {
    }

    std::string Heartbeat::get_port() const
    {
        return get_socket_port(m_heartbeat);
    }

    void Heartbeat::run()
    {
        zmq::pollitem_t items[] = {
            { m_heartbeat, 0, ZMQ_POLLIN, 0 },
            { m_controller, 0, ZMQ_POLLIN, 0 }
        };

        while (true)
        {
            zmq::poll(&items[0], 2, std::chrono::milliseconds(-1));

            if (items[0].revents & ZMQ_POLLIN)
            {
                zmq::multipart_t wire_msg;
                wire_msg.recv(m_heartbeat);
                wire_msg.send(m_heartbeat);
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
    }
}
