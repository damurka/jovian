#ifndef DATASUITE_CONFIGURATION_HPP
#define DATASUITE_CONFIGURATION_HPP

#include <string>
#include <variant>

#include "datasuite.hpp"

namespace datasuite
{
    struct DATASUITE_API CommonConfiguration
    {
        std::string m_transport = "tcp";
        std::string m_ip = "127.0.0.1";
        std::string m_signatureScheme = "hmac-sha256";
        std::string m_key;
    };

    struct DATASUITE_API KernelConfiguration : CommonConfiguration
    {
        std::string m_controlPort;
        std::string m_shellPort;
        std::string m_stdinPort;
        std::string m_iopubPort;
        std::string m_hbPort;
    };

    struct DATASUITE_API RegistrationConfiguration : CommonConfiguration
    {
        std::string m_kernelId;
        std::string m_registrationIp;
        std::string m_registrationPort;
    };

    using configuration = std::variant<KernelConfiguration, RegistrationConfiguration>;

    DATASUITE_API
    configuration loadConfiguration(const std::string& file_name);
}

#endif
