#ifndef SERVER_ZMQ_DEFAULT_HPP
#define SERVER_ZMQ_DEFAULT_HPP

#include "datasuite/server_zmq.hpp"

namespace datasuite
{
    class server_zmq_default final : public server_zmq
    {
    public:

        server_zmq_default(context& context,
            const configuration& config,
            nl::json::error_handler_t eh);

        ~server_zmq_default() override = default;

    private:

        void start_impl(pub_message msg) override;
        void stop_impl() override;
    };
}

#endif
