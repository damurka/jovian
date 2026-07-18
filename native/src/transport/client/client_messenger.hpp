#ifndef DATASUITE_CLIENT_MESSENGER_HPP
#define DATASUITE_CLIENT_MESSENGER_HPP

#include <zmq.hpp>

namespace datasuite
{
    class client_messenger
    {
    public:
        explicit client_messenger(zmq::context_t& context);
        virtual ~client_messenger();

        void connect();
        void stop_channels();

    private:
        zmq::socket_t m_iopub_controller;
        zmq::socket_t m_heartbeat_controller;
    };
}

#endif