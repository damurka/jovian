#include "datasuite/middleware.hpp"

#include "dealer_channel.hpp"

namespace datasuite
{

    dealer_channel::dealer_channel(zmq::context_t& context,
        const std::string& transport,
        const std::string& ip,
        const std::string& port)
        : m_socket(context, zmq::socket_type::dealer)
        , m_dealerEndPoint("")
    {
        m_dealerEndPoint = get_end_point(transport, ip, port);
        m_socket.connect(m_dealerEndPoint);
    }

    dealer_channel::~dealer_channel()
    {
        m_socket.disconnect(m_dealerEndPoint);
    }

    void dealer_channel::send_message(zmq::multipart_t& message)
    {
        message.send(m_socket);
    }

    std::optional<zmq::multipart_t> dealer_channel::receive_message(bool blocking)
    {
        zmq::multipart_t wire_msg;
        zmq::recv_flags flags = zmq::recv_flags::none;

        if (!blocking)
        {
            flags = zmq::recv_flags::dontwait;
        }

        if (wire_msg.recv(m_socket, static_cast<int>(flags)))
        {
            return wire_msg;
        }
        else
        {
            return std::nullopt;
        }
    }

    zmq::socket_t& dealer_channel::get_socket()
    {
        return m_socket;
    }
}
