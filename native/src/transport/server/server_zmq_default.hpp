#ifndef SERVER_ZMQ_DEFAULT_HPP
#define SERVER_ZMQ_DEFAULT_HPP

#include "datasuite/server_zmq.hpp"

namespace datasuite
{
    class ServerZmqDefault final : public ServerZmq
    {
    public:

        ServerZmqDefault(Context& context,
            const configuration& config,
            json::error_handler_t eh);

        ~ServerZmqDefault() override = default;

    private:

        void start_impl(PubMessage msg) override;
        void stop_impl() override;
    };
}

#endif
