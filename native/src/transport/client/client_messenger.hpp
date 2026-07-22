#ifndef DATASUITE_CLIENT_MESSENGER_HPP
#define DATASUITE_CLIENT_MESSENGER_HPP

#include <zmq.hpp>

namespace datasuite
{
    class ClientMessenger
    {
    public:
        explicit ClientMessenger(zmq::context_t& context);
        virtual ~ClientMessenger();

        void connect();
        void stop_channels();

    private:
        zmq::socket_t m_iopubController;
        zmq::socket_t m_heartbeatController;
    };
}

#endif
