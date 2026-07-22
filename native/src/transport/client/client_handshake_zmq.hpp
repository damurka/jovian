#ifndef XHANDSHAKE_CLIENT_ZMQ_HPP
#define XHANDSHAKE_CLIENT_ZMQ_HPP

#include <string>

#include "datasuite/context.hpp"
#include "datasuite/kernel_configuration.hpp"

#include "datasuite/datasuite.hpp"

namespace datasuite
{

    class client_handshake_zmq_impl;

    class DATASUITE_API client_handshake_zmq
    {
    public:

        client_handshake_zmq(context& context, const registration_configuration& config);
        ~client_handshake_zmq();

        std::string get_registration_port() const;

        KernelConfiguration wait_for_configuration();

    private:

        std::unique_ptr<client_handshake_zmq_impl> p_clientImpl;
    };
}

#endif
