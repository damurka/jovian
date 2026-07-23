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
    std::string getControllerEndPoint(const std::string& channel);

    DATASUITE_API
    std::string getPublisherEndPoint();

    DATASUITE_API
    std::string getEndPoint(const std::string& transport,
            const std::string& ip,
            const std::string& port);

    DATASUITE_API
    int getSocketLinger();

    DATASUITE_API
    std::string findFreePort(std::size_t max_tries = 100, int start = 49152, int stop = 65536);
}

#endif