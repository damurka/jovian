#ifndef ADRASTEA_HANSHAKING_HPP
#define ADRASTEA_HANSHAKING_HPP

#include "zmq_addon.hpp"

#include "adrastea/kernel_configuration.hpp"
#include "adrastea/server.hpp"

#include "../common/authentication.hpp"

namespace adrastea
{

    KernelConfiguration getKernelConfiguration(const configuration& config);

    void sendConnectionInfo(
        zmq::context_t& context,
        const RegistrationConfiguration& regis_config,
        const KernelConfiguration& kernel_config,
        const Authentication& auth,
        json::error_handler_t error_handler);
}

#endif
