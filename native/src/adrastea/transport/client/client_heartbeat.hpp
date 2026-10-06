#ifndef ADRASTEA_HEARTBEAT_CLIENT_HPP
#define ADRASTEA_HEARTBEAT_CLIENT_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cstddef>
#include <functional>

#include "zmq.hpp"

#include "adrastea/kernel_configuration.hpp"
#include "heartbeat_status.hpp"

namespace adrastea
{
    class ClientHeartbeat
    {
    public:

        using kernel_status_listener = std::function<void(bool)>;

        ClientHeartbeat(zmq::context_t& context,
            const KernelConfiguration& config,
            const std::size_t max_retry,
            const long timeout);

        ~ClientHeartbeat();

        void run();

        // Returns once run() has begun: a stop sent before that could wait for an answer that never came.
        void waitUntilListening();

        void registerKernelStatusListener(const kernel_status_listener& l);
        void notifyKernelDead(bool status);

        // Safe to call from any thread.
        HeartbeatStatus status() const;

    private:
        void sendHeartbeatMessage();
        bool waitForAnswer(long timeout);

        zmq::socket_t m_heartbeat;
        zmq::socket_t m_controller;

        kernel_status_listener m_kernelStatusListener;
        const std::size_t m_maxRetry;
        const long m_heartbeatTimeout;

        std::string m_heartbeatEndPoint;
        bool m_requestStop;
        std::mutex m_stateMutex;
        std::condition_variable m_stateChanged;
        bool m_listening = false;

        std::atomic<long long> m_lastRttMicros{ -1 };
        std::atomic<long long> m_lastPongMs{ -1 }; // steady_clock, ms since its epoch
        std::atomic<std::size_t> m_misses{ 0 };
    };
}

#endif
