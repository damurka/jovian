#ifndef ADRASTEA_HEARTBEAT_CLIENT_HPP
#define ADRASTEA_HEARTBEAT_CLIENT_HPP

#include <functional>

#include "zmq.hpp"

#include "adrastea/kernel_configuration.hpp"

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

        void registerKernelStatusListener(const kernel_status_listener& l);
        void notifyKernelDead(bool status);

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
    };
}

#endif
