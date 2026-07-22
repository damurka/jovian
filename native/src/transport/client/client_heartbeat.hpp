#ifndef DATASUITE_HEARTBEAT_CLIENT_HPP
#define DATASUITE_HEARTBEAT_CLIENT_HPP

#include <functional>

#include "zmq.hpp"

#include "datasuite/kernel_configuration.hpp"

namespace datasuite
{
    class client_heartbeat
    {
    public:

        using kernel_status_listener = std::function<void(bool)>;

        client_heartbeat(zmq::context_t& context,
            const KernelConfiguration& config,
            const std::size_t max_retry,
            const long timeout);

        ~client_heartbeat();

        void run();

        void register_kernel_status_listener(const kernel_status_listener& l);
        void notify_kernel_dead(bool status);

    private:
        void send_heartbeat_message();
        bool wait_for_answer(long timeout);

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
