#ifndef XINPUT_HPP
#define XINPUT_HPP

#include <string>

#include "adrastea.hpp"

namespace adrastea
{
    // Genuinely blocks the calling (kernel's one execution) thread until a
    // reply arrives -- ServerZmqImpl::sendStdin() does a real, untimed ZMQ
    // recv underneath this. `allowStdin` should be whatever the current
    // execute_request's allow_stdin was (ExecuteRequestConfig::allow_stdin,
    // set by the client per call, default false) -- throws immediately,
    // without sending anything, if false, rather than blocking forever on
    // a reply a caller that never opted into stdin has no way to send.
    ADRASTEA_API
    std::string blockingInputRequest(
        const std::string& prompt,
        bool password,
        bool allowStdin
    );
}

#endif