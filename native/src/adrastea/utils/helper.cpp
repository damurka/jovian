#include <string>
#include <vector>

#include "adrastea/json.hpp"
#include "adrastea/helper.hpp"

namespace adrastea
{
    std::string getStartMessage(const KernelConfiguration& config)
    {
        std::string kernel_info;
        kernel_info = "Starting kernel...\n\n"
            "If you want to connect to this kernel from an other client, just copy"
            " and paste the following content inside of a `kernel.json` file. And then run for example:\n\n"
            "# jupyter console --existing kernel.json\n\n"
            "kernel.json\n```\n{\n"
            "    \"transport\": \"" + config.m_transport + "\",\n"
            "    \"ip\": \"" + config.m_ip + "\",\n"
            "    \"control_port\": " + config.m_controlPort + ",\n"
            "    \"shell_port\": " + config.m_shellPort + ",\n"
            "    \"stdin_port\": " + config.m_stdinPort + ",\n"
            "    \"iopub_port\": " + config.m_iopubPort + ",\n"
            "    \"hb_port\": " + config.m_hbPort + ",\n"
            "    \"signature_scheme\": \"" + config.m_signatureScheme + "\",\n"
            "    \"key\": \"" + config.m_key + "\"\n"
            "}\n```";
        return kernel_info;
    }

    std::string extractFilename(int& argc, char* argv[])
    {
        std::string res = "";
        for (int i = 0; i < argc; ++i)
        {
            if ((std::string(argv[i]) == "-f") && (i + 1 < argc))
            {
                res = argv[i + 1];
                for (int j = i; j < argc - 2; ++j)
                {
                    argv[j] = argv[j + 2];
                }
                argc -= 2;
                break;
            }
        }
        return res;
    }

    bool shouldPrintVersion(int argc, char* argv[])
    {
        for (int i = 0; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--version")
            {
                return true;
            }
        }
        return false;
    }

    // Helpers that create replies to the server
    json createErrorReply(const std::string& ename,
        const std::string& evalue,
        const json& trace_back)
    {
        json kernel_res;
        kernel_res["status"] = "error";
        kernel_res["ename"] = ename;
        kernel_res["evalue"] = evalue;
        kernel_res["traceback"] = trace_back;
        return kernel_res;
    }

    json createSuccessfulReply(const json& payload,
        const json& user_expressions)
    {
        json kernel_res;
        kernel_res["status"] = "ok";
        kernel_res["payload"] = payload;
        kernel_res["user_expressions"] = user_expressions;
        return kernel_res;
    }

    json createCompleteReply(const json& matches,
        const int& cursor_start,
        const int& cursor_end,
        const json& metadata)
    {
        json kernel_res;
        kernel_res["status"] = "ok";
        kernel_res["matches"] = matches;
        kernel_res["cursor_start"] = cursor_start;
        kernel_res["cursor_end"] = cursor_end;
        kernel_res["metadata"] = metadata;
        return kernel_res;
    }

    json createInspectReply(const bool found,
        const json& data,
        const json& metadata)
    {
        json kernel_res;
        kernel_res["status"] = "ok";
        kernel_res["found"] = found;
        kernel_res["data"] = data;
        kernel_res["metadata"] = metadata;
        return kernel_res;
    }

    json createIsCompleteReply(const std::string& status,
        const std::string& indent)
    {
        json kernel_res;
        kernel_res["status"] = status;
        kernel_res["indent"] = indent;
        return kernel_res;
    }

    json createInfoReply(const std::string& implementation,
        const std::string& implementation_version,
        const std::string& language_name,
        const std::string& language_version,
        const std::string& language_mimetype,
        const std::string& language_file_extension,
        const std::string& language_pygments_lexer,
        codemirror_mode_t language_codemirror_mode,
        const std::string& language_nbconvert_exporter,
        const std::string& banner,
        const json& help_links,
        const std::vector<std::string>& supported_features)
    {
        json kernel_res;
        // kernel_res["protocol_version"] is set in KernelCore::kernelInfoRequest
        // to ensure the same version for all the adrastea-based kernels
        kernel_res["status"] = "ok";
        kernel_res["implementation"] = implementation;
        kernel_res["implementation_version"] = implementation_version;
        kernel_res["language_info"]["name"] = language_name;
        kernel_res["language_info"]["version"] = language_version;
        kernel_res["language_info"]["mimetype"] = language_mimetype;
        kernel_res["language_info"]["file_extension"] = language_file_extension;
        kernel_res["language_info"]["pygments_lexer"] = language_pygments_lexer;
        std::visit([&kernel_res](auto&& arg)
            {
                kernel_res["language_info"]["codemirror_mode"] = std::move(arg);
            }, std::move(language_codemirror_mode));
        kernel_res["language_info"]["nbconvert_exporter"] = language_nbconvert_exporter;
        kernel_res["banner"] = banner;
        kernel_res["help_links"] = help_links;
        kernel_res["supported_features"] = supported_features;
        return kernel_res;
    }

    json createShutdownReply(bool restart)
    {
        json kernel_res;
        kernel_res["status"] = "ok";
        kernel_res["restart"] = restart;
        return kernel_res;
    }

    json createInterruptReply()
    {
        json kernel_res;
        kernel_res["status"] = "ok";
        return kernel_res;
    }
}
