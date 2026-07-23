#ifndef DATASUITE_HANSHAKING_HPP
#define DATASUITE_HANSHAKING_HPP

#include "zmq_addon.hpp"

#include "datasuite/kernel_configuration.hpp"
#include "datasuite/server.hpp"

#include "../common/authentication.hpp"

namespace datasuite
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
