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
            const kernel_configuration& config,
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

        kernel_status_listener m_kernel_status_listener;
        const std::size_t m_max_retry;
        const long m_heartbeat_timeout;

        std::string m_heartbeat_end_point;
        bool m_request_stop;
    };
}

#endif
