#include "adrastea/json.hpp"
#include "adrastea/middleware.hpp"
#include "client_messenger.hpp"

namespace adrastea
{
    ClientMessenger::ClientMessenger(zmq::context_t& context)
        : m_iopubController(context, zmq::socket_type::req)
        , m_heartbeatController(context, zmq::socket_type::req)
    {
    }

    ClientMessenger::~ClientMessenger()
    {
    }

    void ClientMessenger::connect()
    {
        m_iopubController.set(zmq::sockopt::linger, getSocketLinger());
        m_iopubController.connect(getControllerEndPoint("iopub"));

        m_heartbeatController.set(zmq::sockopt::linger, getSocketLinger());
        m_heartbeatController.connect(getControllerEndPoint("heartbeat"));
    }

    void ClientMessenger::stopChannels()
    {
        // Bounded rather than the previous unconditional blocking recv():
        // each round trip waits on a reply from the iopub/heartbeat
        // thread's own controller loop (client_iopub.cpp / client_heartbeat.cpp),
        // and a caller that hits any timing edge in that handshake would
        // otherwise hang here forever with no way to recover. 5s is
        // generous relative to how fast this normally completes (well
        // under 100ms in practice).
        m_iopubController.set(zmq::sockopt::rcvtimeo, 5000);
        m_heartbeatController.set(zmq::sockopt::rcvtimeo, 5000);

        // Wait for iopub answer
        {
            zmq::message_t stop_msg("stop", 4);
            zmq::message_t response;
            m_iopubController.send(stop_msg, zmq::send_flags::none);
            (void)m_iopubController.recv(response);
        }

        // Wait for heartbeat answer
        {
            zmq::message_t stop_msg("stop", 4);
            zmq::message_t response;
            m_heartbeatController.send(stop_msg, zmq::send_flags::none);
            (void)m_heartbeatController.recv(response);
        }
    }
}
