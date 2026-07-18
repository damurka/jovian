#include <fstream>
#include <string>

#include "datasuite/json.hpp"
#include "datasuite/kernel_configuration.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    namespace
    {
        void load_common_configuration(const nl::json& doc, common_configuration& res)
        {
            res.m_transport = doc["transport"].get<std::string>();
            res.m_ip = doc["ip"].get<std::string>();
            res.m_signature_scheme = doc.value("signature_scheme", "");
            if (res.m_signature_scheme != "")
            {
                res.m_key = doc["key"].get<std::string>();
            }
            else
            {
                res.m_key = "";
            }
        }

        kernel_configuration load_kernel_configuration(const nl::json& doc)
        {
            kernel_configuration res;
            load_common_configuration(doc, res);
            res.m_control_port = std::to_string(doc["control_port"].get<int>());
            res.m_shell_port = std::to_string(doc["shell_port"].get<int>());
            res.m_stdin_port = std::to_string(doc["stdin_port"].get<int>());
            res.m_iopub_port = std::to_string(doc["iopub_port"].get<int>());
            res.m_hb_port = std::to_string(doc["hb_port"].get<int>());
            return res;
        }

        registration_configuration load_registration_configuration(const nl::json& doc)
        {
            registration_configuration res;
            load_common_configuration(doc, res);
            res.m_kernel_id = doc["kernel_id"].get<std::string>();
            res.m_registration_ip = doc["registration_ip"].get<std::string>();
            res.m_registration_port = doc["registration_port"].get<std::string>();
            return res;
        }
    }

    configuration load_configuration(const std::string& file_name)
    {
        std::ifstream ifs(file_name);

        nl::json doc;
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
