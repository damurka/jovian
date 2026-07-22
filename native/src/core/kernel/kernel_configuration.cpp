#include <fstream>
#include <string>

#include "datasuite/json.hpp"
#include "datasuite/kernel_configuration.hpp"

namespace datasuite
{
    namespace
    {
        void load_common_configuration(const json& doc, common_configuration& res)
        {
            res.m_transport = doc["transport"].get<std::string>();
            res.m_ip = doc["ip"].get<std::string>();
            res.m_signatureScheme = doc.value("signature_scheme", "");
            if (res.m_signatureScheme != "")
            {
                res.m_key = doc["key"].get<std::string>();
            }
            else
            {
                res.m_key = "";
            }
        }

        KernelConfiguration load_kernel_configuration(const json& doc)
        {
            KernelConfiguration res;
            load_common_configuration(doc, res);
            res.m_controlPort = std::to_string(doc["control_port"].get<int>());
            res.m_shellPort = std::to_string(doc["shell_port"].get<int>());
            res.m_stdinPort = std::to_string(doc["stdin_port"].get<int>());
            res.m_iopubPort = std::to_string(doc["iopub_port"].get<int>());
            res.m_hbPort = std::to_string(doc["hb_port"].get<int>());
            return res;
        }

        registration_configuration load_registration_configuration(const json& doc)
        {
            registration_configuration res;
            load_common_configuration(doc, res);
            res.m_kernelId = doc["kernel_id"].get<std::string>();
            res.m_registrationIp = doc["registration_ip"].get<std::string>();
            res.m_registrationPort = doc["registration_port"].get<std::string>();
            return res;
        }
    }

    configuration load_configuration(const std::string& file_name)
    {
        std::ifstream ifs(file_name);

        json doc;
        ifs >> doc;

        std::string registration_ip = doc.value("registration_ip", "");
        if (registration_ip != "")
        {
            return load_registration_configuration(doc);
        }
        else
        {
            return load_kernel_configuration(doc);
        }
    }
}
