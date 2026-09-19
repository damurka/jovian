#ifndef ADRASTEA_CONFIGURATION_HPP
#define ADRASTEA_CONFIGURATION_HPP

#include <string>
#include <variant>

#include "adrastea.hpp"

namespace adrastea
{
    struct ADRASTEA_API CommonConfiguration
    {
        std::string m_transport = "tcp";
        std::string m_ip = "127.0.0.1";
        std::string m_signatureScheme = "hmac-sha256";
        std::string m_key;
    };

    struct ADRASTEA_API KernelConfiguration : CommonConfiguration
    {
        std::string m_controlPort;
        std::string m_shellPort;
        std::string m_stdinPort;
        std::string m_iopubPort;
        std::string m_hbPort;
    };

    struct ADRASTEA_API RegistrationConfiguration : CommonConfiguration
    {
        std::string m_kernelId;
        std::string m_registrationIp;
        std::string m_registrationPort;
    };

    using configuration = std::variant<KernelConfiguration, RegistrationConfiguration>;

    ADRASTEA_API
    configuration loadConfiguration(const std::string& file_name);
}

#endif
