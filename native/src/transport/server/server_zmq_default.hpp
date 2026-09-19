#ifndef SERVER_ZMQ_DEFAULT_HPP
#define SERVER_ZMQ_DEFAULT_HPP

#include "adrastea/server_zmq.hpp"

namespace adrastea
{
    class ServerZmqDefault final : public ServerZmq
    {
    public:

        ServerZmqDefault(Context& context,
            const configuration& config,
            json::error_handler_t eh);

        ~ServerZmqDefault() override = default;

    private:

        void startImpl(PubMessage msg) override;
        void stopImpl() override;
    };
}

#endif
