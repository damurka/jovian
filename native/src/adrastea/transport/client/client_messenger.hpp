#ifndef ADRASTEA_CLIENT_MESSENGER_HPP
#define ADRASTEA_CLIENT_MESSENGER_HPP

#include <zmq.hpp>

namespace adrastea
{
    class ClientMessenger
    {
    public:
        explicit ClientMessenger(zmq::context_t& context);
        virtual ~ClientMessenger();

        void connect();
        void stopChannels();

    private:
        zmq::socket_t m_iopubController;
        zmq::socket_t m_heartbeatController;
    };
}

#endif
