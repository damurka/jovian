#include "nlohmann/json.hpp"
#include "datasuite/middleware.hpp"
#include "client_messenger.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    client_messenger::client_messenger(zmq::context_t& context)
        : m_iopub_controller(context, zmq::socket_type::req)
        , m_heartbeat_controller(context, zmq::socket_type::req)
    {
    }

    client_messenger::~client_messenger()
    {
    }

    void client_messenger::connect()
    {
        m_iopub_controller.set(zmq::sockopt::linger, get_socket_linger());
        m_iopub_controller.connect(get_controller_end_point("iopub"));

        m_heartbeat_controller.set(zmq::sockopt::linger, get_socket_linger());
        m_heartbeat_controller.connect(get_controller_end_point("heartbeat"));
    }

    void client_messenger::stop_channels()
    {
        zmq::message_t stop_msg("stop", 4);
        zmq::message_t response;

        // Wait for iopub answer
        m_iopub_controller.send(stop_msg, zmq::send_flags::none);
        (void)m_iopub_controller.recv(response);

        // Wait for heartbeat answer
        m_heartbeat_controller.send(stop_msg, zmq::send_flags::none);
        (void)m_heartbeat_controller.recv(response);
    }
}
