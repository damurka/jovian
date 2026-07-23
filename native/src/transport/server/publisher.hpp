#ifndef DATASUITE_PUBLISHER_HPP
#define DATASUITE_PUBLISHER_HPP

#include <functional>
#include <string>

#include "zmq.hpp"
#include "zmq_addon.hpp"

#include "datasuite/message.hpp"

namespace datasuite
{
    class Publisher
    {
    public:

        Publisher(zmq::context_t& context,
            std::function<zmq::multipart_t(PubMessage&&)> serialize_iopub_msg_cb,
            const std::string& transport,
            const std::string& ip,
            const std::string& port);

        ~Publisher();

        std::string getPort() const;

        void run();

    private:

        PubMessage createPubMessage(const std::string& topic);

        zmq::socket_t m_publisher;
        zmq::socket_t m_listener;
        zmq::socket_t m_controller;

        std::function<zmq::multipart_t(PubMessage&&)> m_serializeIopubMsgCb;
    };
}

#endif
