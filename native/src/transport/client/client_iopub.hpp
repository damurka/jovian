#ifndef DATASUITE_IOPUB_CLIENT_HPP
#define DATASUITE_IOPUB_CLIENT_HPP

#include <queue>
#include <mutex>

#include "zmq.hpp"

#include "datasuite/message.hpp"
#include "datasuite/kernel_configuration.hpp"

namespace datasuite
{
    class ClientZmqImpl;

    class ClientIopub
    {
    public:

        ClientIopub(zmq::context_t& context,
            const KernelConfiguration& config,
            ClientZmqImpl* client);

        ~ClientIopub();

        std::size_t iopubQueueSize() const;
        std::optional<PubMessage> popIopubMessage();

        void run();

    private:
        zmq::socket_t m_iopub;
        zmq::socket_t m_controller;

        std::string m_iopubEndPoint;

        std::queue<PubMessage> m_messageQueue;
        mutable std::mutex m_queueMutex;

        ClientZmqImpl* p_clientImpl;
    };
}

#endif
