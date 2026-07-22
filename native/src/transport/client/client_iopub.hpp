#ifndef DATASUITE_IOPUB_CLIENT_HPP
#define DATASUITE_IOPUB_CLIENT_HPP

#include <queue>
#include <mutex>

#include "zmq.hpp"

#include "datasuite/message.hpp"
#include "datasuite/kernel_configuration.hpp"

namespace datasuite
{
    class client_zmq_impl;

    class client_iopub
    {
    public:

        client_iopub(zmq::context_t& context,
            const KernelConfiguration& config,
            client_zmq_impl* client);

        ~client_iopub();

        std::size_t iopub_queue_size() const;
        std::optional<pub_message> pop_iopub_message();

        void run();

    private:
        zmq::socket_t m_iopub;
        zmq::socket_t m_controller;

        std::string m_iopubEndPoint;

        std::queue<pub_message> m_messageQueue;
        mutable std::mutex m_queueMutex;

        client_zmq_impl* p_clientImpl;
    };
}

#endif
