#ifndef XHANDSHAKE_CLIENT_ZMQ_HPP
#define XHANDSHAKE_CLIENT_ZMQ_HPP

#include <string>

#include "datasuite/context.hpp"
#include "datasuite/kernel_configuration.hpp"

#include "datasuite/datasuite.hpp"

namespace datasuite
{

    class ClientHandshakeZmqImpl;

    class DATASUITE_API ClientHandshakeZmq
    {
    public:

        ClientHandshakeZmq(Context& context, const RegistrationConfiguration& config);
        ~ClientHandshakeZmq();

        std::string get_registration_port() const;

        KernelConfiguration wait_for_configuration();

    private:

        std::unique_ptr<ClientHandshakeZmqImpl> p_clientImpl;
    };
}

#endif
