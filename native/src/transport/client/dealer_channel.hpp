#ifndef DATASUITE_DEALER_CHANNEL_HPP
#define DATASUITE_DEALER_CHANNEL_HPP

#include "zmq.hpp"
#include "zmq_addon.hpp"

namespace datasuite
{

    class dealer_channel
    {
    public:

        dealer_channel(zmq::context_t& context,
            const std::string& transport,
            const std::string& ip,
            const std::string& port);

        ~dealer_channel();

        void send_message(zmq::multipart_t& message);
        std::optional<zmq::multipart_t> receive_message(bool blocking);

        zmq::socket_t& get_socket();

    private:

        zmq::socket_t m_socket;
        std::string m_dealer_end_point;
    };
}

#endif
