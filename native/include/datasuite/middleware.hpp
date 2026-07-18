#ifndef DATASUITE_MIDDLEWARE_HPP
#define DATASUITE_MIDDLEWARE_HPP

#include <string>

#include "datasuite.hpp"

namespace datasuite
{

    // Duplicate definition for cppzmq
#if defined _WIN64
    using fd_t = __int64;
#elif defined _WIN32
    using fd_t = unsinged int;
#else
    using fd_t = int;
#endif

    DATASUITE_API
    std::string get_controller_end_point(const std::string& channel);

    DATASUITE_API
    std::string get_publisher_end_point();

    DATASUITE_API
    std::string get_end_point(const std::string& transport,
            const std::string& ip,
            const std::string& port);

    DATASUITE_API
    int get_socket_linger();

    DATASUITE_API
    std::string find_free_port(std::size_t max_tries = 100, int start = 49152, int stop = 65536);
}

#endif