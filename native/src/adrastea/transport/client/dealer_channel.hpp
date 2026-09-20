#ifndef ADRASTEA_DEALER_CHANNEL_HPP
#define ADRASTEA_DEALER_CHANNEL_HPP

#include <mutex>
#include <optional>
#include <string>

#include "zmq.hpp"
#include "zmq_addon.hpp"

namespace adrastea
{

    class DealerChannel
    {
    public:

        // `identity`, when non-empty, is set as this DEALER socket's own
        // ZMQ_ROUTING_ID before connecting -- otherwise ZMQ auto-assigns a
        // random one per socket. Needed so a kernel-side ROUTER can be
        // addressed later using an identity CAPTURED from a DIFFERENT
        // channel's incoming message (see ClientZmqImpl's constructor
        // comment: KernelCore::sendStdin() reuses the identity it captured
        // from the execute_request that arrived on shell to address the
        // input_request it sends on stdin -- that only resolves to a real
        // connected peer if the stdin DEALER presents that same identity).
        DealerChannel(zmq::context_t& context,
            const std::string& transport,
            const std::string& ip,
            const std::string& port,
            const std::string& identity = "");

        ~DealerChannel();

        void sendMessage(zmq::multipart_t& message);
        std::optional<zmq::multipart_t> receiveMessage(bool blocking);

        zmq::socket_t& getSocket();

    private:

        zmq::socket_t m_socket;
        std::string m_dealerEndPoint;
        std::mutex m_mutex;
    };
}

#endif
