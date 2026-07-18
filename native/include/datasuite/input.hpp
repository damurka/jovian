#ifndef XINPUT_HPP
#define XINPUT_HPP

#include <string>

#include "datasuite.hpp"

namespace datasuite
{
    DATASUITE_API
    std::string blocking_input_request(
        const std::string& prompt,
        bool password
    );
}

#endif