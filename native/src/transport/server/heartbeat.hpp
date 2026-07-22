#ifndef DATASUITE_HEARTBEAT_HPP
#define DATASUITE_HEARTBEAT_HPP

#include <string>

#include "zmq.hpp"

namespace datasuite
{
    class Heartbeat
    {
    public:

        Heartbeat(zmq::context_t& context,
            const std::string& transport,
            const std::string& ip,
            const std::string& port);

        ~Heartbeat();

        std::string get_port() const;

        void run();

    private:

        zmq::socket_t m_heartbeat;
        zmq::socket_t m_controller;
    };
}

#endif
