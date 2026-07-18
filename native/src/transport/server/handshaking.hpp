#ifndef DATASUITE_HANSHAKING_HPP
#define DATASUITE_HANSHAKING_HPP

#include "zmq_addon.hpp"

#include "datasuite/kernel_configuration.hpp"
#include "datasuite/server.hpp"

#include "../common/authentication.hpp"

namespace datasuite
{

    kernel_configuration get_kernel_configuration(const configuration& config);

    void send_connection_info(
        zmq::context_t& context,
        const registration_configuration& regis_config,
        const kernel_configuration& kernel_config,
        const authentication& auth,
        nl::json::error_handler_t error_handler);
}

#endif
