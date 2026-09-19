#ifndef ADRASTEA_SYSTEM_HPP
#define ADRASTEA_SYSTEM_HPP

#include <string>

#include "adrastea.hpp"

namespace adrastea 
{
	ADRASTEA_API
	std::string getTempDirectoryPath();

    ADRASTEA_API
    bool createDirectory(const std::string& path);

    ADRASTEA_API
    int getCurrentPid();

    ADRASTEA_API
    std::size_t getTmpHashSeed();

    ADRASTEA_API
    std::string getTmpPrefix(const std::string& process_name);

    ADRASTEA_API
    std::string executablePath();

    ADRASTEA_API
    std::string prefixPath();
}

#endif // !ADRASTEA_SYSTEM_HPP
