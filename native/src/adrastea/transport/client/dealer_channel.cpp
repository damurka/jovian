#include <chrono>
#include <thread>

#include "adrastea/middleware.hpp"

#include "dealer_channel.hpp"

namespace adrastea
{

    DealerChannel::DealerChannel(zmq::context_t& context,
        const std::string& transport,
        const std::string& ip,
        const std::string& port,
        const std::string& identity)
        : m_socket(context, zmq::socket_type::dealer)
        , m_dealerEndPoint("")
    {
        // ZMQ_LINGER defaults to -1 (infinite) when never set -- meaning
        // closing/destructing this socket (Session::~Session() tearing down
        // its ClientZmq, ultimately here) blocks until every queued-but-
        // unacknowledged outbound message is flushed, with NO timeout, if
        // the peer never acknowledges it. A genuinely dead kernel (crashed,
        // externally killed) can never acknowledge anything again -- so a
        // control message sent to it right before teardown (e.g.
        // stopSession()'s own shutdown_request, sent unconditionally even
        // to an already-crashed session) could leave this socket blocking
        // forever on destruction. Confirmed hit for real: an intermittent
        // hang, timing-dependent on whether that send had already been
        // recognized as undeliverable by the time teardown started, only
        // ever surfacing for a session whose kernel died before stopSession()
        // ran on it. Matches getSocketLinger()'s existing 1000ms convention
        // (adrastea/middleware.hpp), already used by every other socket in
        // this codebase for exactly this reason -- this was the one socket
        // type that had never picked it up.
        m_socket.set(zmq::sockopt::linger, getSocketLinger());
        if (!identity.empty())
        {
            m_socket.set(zmq::sockopt::routing_id, identity);
        }
        m_dealerEndPoint = getEndPoint(transport, ip, port);
        m_socket.connect(m_dealerEndPoint);
    }

    DealerChannel::~DealerChannel()
    {
        m_socket.disconnect(m_dealerEndPoint);
    }

    void DealerChannel::sendMessage(zmq::multipart_t& message)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        message.send(m_socket);
    }

    std::optional<zmq::multipart_t> DealerChannel::receiveMessage(bool blocking)
    {
        // A ZMQ socket must never be used from two threads at once, and
        // Themisto does exactly that: the poll thread receives on each
        // channel while HTTP/WS threads send on it (execute/request/
        // shutdown). A blocking recv can't simply hold the mutex for its
        // whole wait (it would starve every send), so it is a short
        // non-blocking attempt in a loop instead.
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                zmq::multipart_t wire_msg;
                if (wire_msg.recv(m_socket, static_cast<int>(zmq::recv_flags::dontwait)))
                {
                    return wire_msg;
                }
            }
            if (!blocking)
            {
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    zmq::socket_t& DealerChannel::getSocket()
    {
        return m_socket;
    }
}
