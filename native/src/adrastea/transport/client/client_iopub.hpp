#ifndef ADRASTEA_IOPUB_CLIENT_HPP
#define ADRASTEA_IOPUB_CLIENT_HPP

#include <atomic>
#include <queue>
#include <mutex>

#include "zmq.hpp"

#include "adrastea/message.hpp"
#include "adrastea/kernel_configuration.hpp"

namespace adrastea
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

        // Readable once a message was queued since the last
        // clearQueuedSignal(); for the one thread that waits (and pops).
        zmq::socket_t& queuedSignal();
        void clearQueuedSignal();

    private:
        zmq::socket_t m_iopub;
        zmq::socket_t m_controller;
        // inproc pair: the receiving thread signals, the waiting thread is woken
        zmq::socket_t m_signalSend;
        zmq::socket_t m_signalReceive;
        std::atomic<bool> m_signalled{ false };

        std::string m_iopubEndPoint;

        std::queue<PubMessage> m_messageQueue;
        mutable std::mutex m_queueMutex;

        ClientZmqImpl* p_clientImpl;
    };
}

#endif
