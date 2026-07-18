#ifndef DATASUITE_MIDDLEWARE_IMPL_HPP
#define DATASUITE_MIDDLEWARE_IMPL_HPP

#include <string>
#include "zmq.hpp"

#include "datasuite/middleware.hpp"

namespace datasuite
{
    void init_socket(zmq::socket_t& socket,
        const std::string& transport,
        const std::string& ip,
        const std::string& port);

    void init_socket(zmq::socket_t& socket, const std::string& end_point);

    std::string get_socket_port(const zmq::socket_t& socket);
}

#endif
