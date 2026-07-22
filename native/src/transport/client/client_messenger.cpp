#include "datasuite/json.hpp"
#include "datasuite/middleware.hpp"
#include "client_messenger.hpp"

namespace datasuite
{
    client_messenger::client_messenger(zmq::context_t& context)
        : m_iopubController(context, zmq::socket_type::req)
        , m_heartbeatController(context, zmq::socket_type::req)
    {
    }

    client_messenger::~client_messenger()
    {
    }

    void client_messenger::connect()
    {
        m_iopubController.set(zmq::sockopt::linger, get_socket_linger());
        m_iopubController.connect(get_controller_end_point("iopub"));

        m_heartbeatController.set(zmq::sockopt::linger, get_socket_linger());
        m_heartbeatController.connect(get_controller_end_point("heartbeat"));
    }

    void client_messenger::stop_channels()
    {
        zmq::message_t stop_msg("stop", 4);
        zmq::message_t response;

        // Wait for iopub answer
        m_iopubController.send(stop_msg, zmq::send_flags::none);
        (void)m_iopubController.recv(response);

        // Wait for heartbeat answer
        m_heartbeatController.send(stop_msg, zmq::send_flags::none);
        (void)m_heartbeatController.recv(response);
    }
}
