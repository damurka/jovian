#ifndef ADRASTEA_MIDDLEWARE_HPP
#define ADRASTEA_MIDDLEWARE_HPP

#include <string>

#include "adrastea.hpp"

namespace adrastea
{

    // Duplicate definition for cppzmq
#if defined _WIN64
    using fd_t = __int64;
#elif defined _WIN32
    using fd_t = unsinged int;
#else
    using fd_t = int;
#endif

    ADRASTEA_API
    std::string getControllerEndPoint(const std::string& channel);

    ADRASTEA_API
    std::string getPublisherEndPoint();

    ADRASTEA_API
    std::string getEndPoint(const std::string& transport,
            const std::string& ip,
            const std::string& port);

    ADRASTEA_API
    int getSocketLinger();

    ADRASTEA_API
    std::string findFreePort(std::size_t max_tries = 100, int start = 49152, int stop = 65536);
}

#endif