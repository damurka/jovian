#ifndef THEMISTO_ACTIVITY_HPP
#define THEMISTO_ACTIVITY_HPP

#include <atomic>
#include <chrono>
#include <cstdint>

namespace themisto
{
    // When a client last did anything, and how many WebSocket connections
    // are open -- what --idle-shutdown-minutes (main.cpp) decides by. Updated
    // from the HTTP and WebSocket threads.
    class Activity
    {
    public:
        Activity() { touch(); }

        void touch() { m_lastMs = nowMs(); }
        void connectionOpened() { ++m_connections; touch(); }
        void connectionClosed() { --m_connections; touch(); }

        int openConnections() const { return m_connections.load(); }
        std::int64_t idleMs() const { return nowMs() - m_lastMs.load(); }

    private:
        static std::int64_t nowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        std::atomic<std::int64_t> m_lastMs{ 0 };
        std::atomic<int> m_connections{ 0 };
    };
}

#endif
