#ifndef XINPUT_HPP
#define XINPUT_HPP

#include <string>

#include "datasuite.hpp"

namespace datasuite
{
    DATASUITE_API
    std::string blockingInputRequest(
        const std::string& prompt,
        bool password
    );
}

#endif