#ifndef ADRASTEA_HEARTBEAT_HPP
#define ADRASTEA_HEARTBEAT_HPP

#include <string>

#include "zmq.hpp"

namespace adrastea
{
    class Heartbeat
    {
    public:

        Heartbeat(zmq::context_t& context,
            const std::string& transport,
            const std::string& ip,
            const std::string& port);

        ~Heartbeat();

        std::string getPort() const;

        void run();

    private:

        zmq::socket_t m_heartbeat;
        zmq::socket_t m_controller;
    };
}

#endif
