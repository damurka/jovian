#ifndef DATASUITE_SYSTEM_HPP
#define DATASUITE_SYSTEM_HPP

#include <string>

#include "datasuite.hpp"

namespace datasuite 
{
	DATASUITE_API
	std::string getTempDirectoryPath();

    DATASUITE_API
    bool createDirectory(const std::string& path);

    DATASUITE_API
    int getCurrentPid();

    DATASUITE_API
    std::size_t getTmpHashSeed();

    DATASUITE_API
    std::string getTmpPrefix(const std::string& process_name);

    DATASUITE_API
    std::string executablePath();

    DATASUITE_API
    std::string prefixPath();
}

#endif // !DATASUITE_SYSTEM_HPP
