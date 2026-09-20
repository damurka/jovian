#ifndef ADRASTEA_HEARTBEAT_STATUS_HPP
#define ADRASTEA_HEARTBEAT_STATUS_HPP

#include <cstddef>

namespace adrastea
{
    // What the client's heartbeat channel last saw. The kernel answers
    // pings from its own thread, so this stays fresh even while the kernel
    // is busy executing code -- that is what makes it a liveness signal
    // rather than a responsiveness one.
    struct HeartbeatStatus
    {
        // False until the first pong has arrived.
        bool hasPong = false;
        // Round-trip time of the most recent answered ping.
        double rttMs = 0.0;
        // How long ago that pong arrived.
        long long sinceLastPongMs = 0;
        // Pings in a row that went unanswered (0 = healthy).
        std::size_t misses = 0;
    };
}

#endif
