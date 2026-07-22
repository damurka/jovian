#ifndef DATASUITE_PUBLISHER_HPP
#define DATASUITE_PUBLISHER_HPP

#include <functional>
#include <string>

#include "zmq.hpp"
#include "zmq_addon.hpp"

#include "datasuite/message.hpp"

namespace datasuite
{
    class publisher
    {
    public:

        publisher(zmq::context_t& context,
            std::function<zmq::multipart_t(pub_message&&)> serialize_iopub_msg_cb,
            const std::string& transport,
            const std::string& ip,
            const std::string& port);

        ~publisher();

        std::string get_port() const;

        void run();

    private:

        pub_message create_pub_message(const std::string& topic);

        zmq::socket_t m_publisher;
        zmq::socket_t m_listener;
        zmq::socket_t m_controller;

        std::function<zmq::multipart_t(pub_message&&)> m_serializeIopubMsgCb;
    };
}

#endif
