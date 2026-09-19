#ifndef XINPUT_HPP
#define XINPUT_HPP

#include <string>

#include "adrastea.hpp"

namespace adrastea
{
    ADRASTEA_API
    std::string blockingInputRequest(
        const std::string& prompt,
        bool password
    );
}

#endif