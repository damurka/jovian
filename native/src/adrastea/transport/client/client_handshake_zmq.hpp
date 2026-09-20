#ifndef XHANDSHAKE_CLIENT_ZMQ_HPP
#define XHANDSHAKE_CLIENT_ZMQ_HPP

#include <functional>
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

        // Polls for the kernel's registration in short intervals rather than
        // one long blocking recv, so a caller that knows how to check the
        // spawned kernel process's own liveness (e.g. SessionRegistry, via
        // KernelProcess::isAlive()) can pass `shouldAbort` and get a fast,
        // specific failure the moment that process dies, instead of waiting
        // out the full timeout for a registration that can now never arrive.
        // Still bounded by an overall timeout regardless (covers a process
        // that's alive but never registers for some other reason, or a
        // caller that passes no predicate at all).
        KernelConfiguration waitForConfiguration(const std::function<bool()>& shouldAbort = nullptr);

    private:

        std::unique_ptr<ClientHandshakeZmqImpl> p_clientImpl;
    };
}

#endif
