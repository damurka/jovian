// Standalone Jupyter-protocol Stata kernel executable ("callisto") -- see
// callisto/interpreter_stata.hpp. Same launch modes as carpo.cpp: a
// registration handshake with a supervisor, or a standard Jupyter connection
// file.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

#include "zmq.hpp"

#include "adrastea/context.hpp"
#include "callisto/engine.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/message.hpp"
#include "adrastea/middleware.hpp"

#include "adrastea/transport/common/authentication.hpp"
#include "adrastea/transport/server/handshaking.hpp"

namespace
{
    struct CliOptions
    {
        std::string stataHome;
        std::string stataEdition;
        std::string registrationIp = "127.0.0.1";
        std::string registrationPort;
        std::string key;
        std::string connectionFile;
    };

    std::unordered_map<std::string, std::string> parseArgs(int argc, char* argv[])
    {
        std::unordered_map<std::string, std::string> args;
        for (int i = 1; i + 1 < argc; i += 2)
        {
            std::string flag = argv[i];
            if (flag == "-f")
            {
                args["connection-file"] = argv[i + 1];
            }
            else if (flag.rfind("--", 0) == 0)
            {
                args[flag.substr(2)] = argv[i + 1];
            }
        }
        return args;
    }

    CliOptions toCliOptions(const std::unordered_map<std::string, std::string>& args)
    {
        CliOptions opts;
        auto get = [&](const char* name) -> std::string {
            auto it = args.find(name);
            return it == args.end() ? std::string() : it->second;
        };
        opts.stataHome = get("stata-home");
        opts.stataEdition = get("stata-edition");
        if (auto ip = get("registration-ip"); !ip.empty())
        {
            opts.registrationIp = ip;
        }
        opts.registrationPort = get("registration-port");
        opts.key = get("key");
        opts.connectionFile = get("connection-file");
        return opts;
    }

    adrastea::KernelConfiguration makeKernelConfiguration(const std::string& key)
    {
        adrastea::KernelConfiguration config;
        config.m_transport = "tcp";
        config.m_ip = "127.0.0.1";
        config.m_shellPort = adrastea::findFreePort();
        config.m_controlPort = adrastea::findFreePort();
        config.m_stdinPort = adrastea::findFreePort();
        config.m_iopubPort = adrastea::findFreePort();
        config.m_hbPort = adrastea::findFreePort();
        config.m_signatureScheme = "hmac-sha256";
        config.m_key = key;
        return config;
    }
}

int main(int argc, char* argv[])
{
    CliOptions opts = toCliOptions(parseArgs(argc, argv));
    const bool standaloneJupyterMode = !opts.connectionFile.empty();

    if (!standaloneJupyterMode && (opts.registrationPort.empty() || opts.key.empty()))
    {
        std::cerr << "[callisto] --registration-port and --key are required unless -f/--connection-file is "
                     "given."
                  << std::endl;
        return 1;
    }

    callisto::EnvironmentConfig envConfig;
    envConfig.stata_home = opts.stataHome;
    envConfig.stata_edition = opts.stataEdition;

    adrastea::KernelConfiguration kernelConfig;

    if (standaloneJupyterMode)
    {
        adrastea::configuration parsed;
        try
        {
            parsed = adrastea::loadConfiguration(opts.connectionFile);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[callisto] FATAL: failed to read connection file " << opts.connectionFile << ": "
                      << e.what() << std::endl;
            return 1;
        }
        if (!std::holds_alternative<adrastea::KernelConfiguration>(parsed))
        {
            std::cerr << "[callisto] FATAL: " << opts.connectionFile
                      << " is a registration-style connection file (has registration_ip) -- that mode is "
                         "for a supervisor, launched via --registration-port/--key instead of -f."
                      << std::endl;
            return 1;
        }
        kernelConfig = std::get<adrastea::KernelConfiguration>(parsed);
    }
    else
    {
        kernelConfig = makeKernelConfiguration(opts.key);
    }

    callisto::Server server(envConfig);

    try
    {
        if (standaloneJupyterMode)
        {
            server.start(kernelConfig, [&]() {
                std::cerr << "[callisto] ready (Jupyter connection-file mode), shell="
                          << kernelConfig.m_shellPort << " control=" << kernelConfig.m_controlPort
                          << " iopub=" << kernelConfig.m_iopubPort << std::endl;
            });
        }
        else
        {
            server.start(kernelConfig, [&]() {
                auto handshakeContext = adrastea::makeZmqContext();
                auto& zmqContext = handshakeContext->getWrappedContext<zmq::context_t>();

                adrastea::RegistrationConfiguration regConfig;
                regConfig.m_transport = "tcp";
                regConfig.m_signatureScheme = "hmac-sha256";
                regConfig.m_key = opts.key;
                regConfig.m_registrationIp = opts.registrationIp;
                regConfig.m_registrationPort = opts.registrationPort;

                auto auth = adrastea::makeAuthentication("hmac-sha256", opts.key);

                adrastea::sendConnectionInfo(zmqContext, regConfig, kernelConfig, *auth, adrastea::json::error_handler_t::strict);
                std::cerr << "[callisto] registered with supervisor at "
                          << opts.registrationIp << ":" << opts.registrationPort << std::endl;
            });
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[callisto] FATAL: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
