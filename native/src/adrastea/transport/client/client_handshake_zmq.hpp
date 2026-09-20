#ifndef XHANDSHAKE_CLIENT_ZMQ_HPP
#define XHANDSHAKE_CLIENT_ZMQ_HPP

#include <string>

#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"

#include "adrastea/adrastea.hpp"

namespace adrastea
{

    class ClientHandshakeZmqImpl;

    class ADRASTEA_API ClientHandshakeZmq
    {
    public:

        ClientHandshakeZmq(Context& context, const RegistrationConfiguration& config);
        ~ClientHandshakeZmq();

        std::string getRegistrationPort() const;

        KernelConfiguration waitForConfiguration();

    private:

        std::unique_ptr<ClientHandshakeZmqImpl> p_clientImpl;
    };
}

#endif
