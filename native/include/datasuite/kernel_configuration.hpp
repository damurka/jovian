#ifndef DATASUITE_CONFIGURATION_HPP
#define DATASUITE_CONFIGURATION_HPP

#include <string>
#include <variant>

#include "datasuite.hpp"

namespace datasuite
{
    struct DATASUITE_API common_configuration
    {
        std::string m_transport = "tcp";
        std::string m_ip = "127.0.0.1";
        std::string m_signature_scheme = "hmac-sha256";
        std::string m_key;
    };

    struct DATASUITE_API kernel_configuration : common_configuration
    {
        std::string m_control_port;
        std::string m_shell_port;
        std::string m_stdin_port;
        std::string m_iopub_port;
        std::string m_hb_port;
    };

    struct DATASUITE_API registration_configuration : common_configuration
    {
        std::string m_kernel_id;
        std::string m_registration_ip;
        std::string m_registration_port;
    };

    using configuration = std::variant<kernel_configuration, registration_configuration>;

    DATASUITE_API
    configuration load_configuration(const std::string& file_name);
}

#endif