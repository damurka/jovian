#ifndef ADRASTEA_IOPUB_CLIENT_HPP
#define ADRASTEA_IOPUB_CLIENT_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
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

        // Returns once run() polls: a stop sent before that could wait for an answer that never came.
        void waitUntilListening();
        // Whether the kernel's iopub_welcome (its answer to this client's subscription) has arrived within
        // `timeout`: from then on nothing the kernel publishes is missed. A kernel that sends none (not every
        // Jupyter kernel does) is simply waited for in vain.
        bool waitForWelcome(std::chrono::milliseconds timeout);

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
        std::mutex m_stateMutex;
        std::condition_variable m_stateChanged;
        bool m_listening = false;
        bool m_welcomed = false;

        std::string m_iopubEndPoint;

        std::queue<PubMessage> m_messageQueue;
        mutable std::mutex m_queueMutex;

        ClientZmqImpl* p_clientImpl;
    };
}

#endif
