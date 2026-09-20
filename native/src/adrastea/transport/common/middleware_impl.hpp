#ifndef ADRASTEA_MIDDLEWARE_IMPL_HPP
#define ADRASTEA_MIDDLEWARE_IMPL_HPP

#include <string>
#include "zmq.hpp"

#include "adrastea/middleware.hpp"

namespace adrastea
{
    void initSocket(zmq::socket_t& socket,
        const std::string& transport,
        const std::string& ip,
        const std::string& port);

    void initSocket(zmq::socket_t& socket, const std::string& end_point);

    std::string getSocketPort(const zmq::socket_t& socket);
}

#endif
