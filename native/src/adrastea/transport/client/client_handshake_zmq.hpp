#ifndef ADRASTEA_CLIENT_HANDSHAKE_ZMQ_HPP
#define ADRASTEA_CLIENT_HANDSHAKE_ZMQ_HPP

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
        //
        // A registration is accepted only when it is signed with this listener's key and, given
        // `expectedKernelId`, when it is that kernel's (the id the supervisor passed it as --registration-id):
        // the registration socket is one for every kernel, and a stale or foreign registration must not be
        // taken for the one awaited. A refused one is answered so (its sender ends) and the wait goes on.
        KernelConfiguration waitForConfiguration(const std::function<bool()>& shouldAbort = nullptr,
            const std::string& expectedKernelId = std::string());

    private:

        std::unique_ptr<ClientHandshakeZmqImpl> p_clientImpl;
    };
}

#endif
