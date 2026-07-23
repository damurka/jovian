#ifndef DATASUITE_DEALER_CHANNEL_HPP
#define DATASUITE_DEALER_CHANNEL_HPP

#include "zmq.hpp"
#include "zmq_addon.hpp"

namespace datasuite
{

    class DealerChannel
    {
    public:

        DealerChannel(zmq::context_t& context,
            const std::string& transport,
            const std::string& ip,
            const std::string& port);

        ~DealerChannel();

        void sendMessage(zmq::multipart_t& message);
        std::optional<zmq::multipart_t> receiveMessage(bool blocking);

        zmq::socket_t& getSocket();

    private:

        zmq::socket_t m_socket;
        std::string m_dealerEndPoint;
    };
}

#endif
