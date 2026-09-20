// Standalone Jupyter-protocol R kernel executable ("elara").
//
// Reuses elara::Server (native/src/elara/bridge/engine.cpp) verbatim
// for environment setup and booting the embedded R Kernel on a background
// thread -- that class has no N-API dependency (only bridge/addon.cpp does),
// so it's shared between this executable and the Node addon rather than
// duplicated. What this file adds on top is the piece the addon never
// needed: registering with a supervisor process over ZMQ so the supervisor
// (not an in-process Client) becomes this kernel's client. See
// native/src/adrastea/transport/server/handshaking.cpp's sendConnectionInfo, which
// already implements the "kernel dials home with its bound ports" protocol
// used here -- it existed unused until this executable started calling it.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <string>
#include <unordered_map>

#include "zmq.hpp"

#include "adrastea/context.hpp"
#include "elara/engine.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/message.hpp"
#include "adrastea/middleware.hpp"

#include "adrastea/transport/common/authentication.hpp"
#include "adrastea/transport/server/handshaking.hpp"

namespace
{
    struct CliOptions
    {
        std::string rHome;
        std::string rPath;
        std::string rLibs;
        std::string pandocPath;
        std::string heraSrcPath;
        std::string registrationIp = "127.0.0.1";
        std::string registrationPort;
        std::string key;
        // Standard Jupyter kernel launch mode (a frontend -- JupyterLab,
        // `jupyter console`, etc. -- writes this file with pre-chosen ports
        // and runs this executable's kernelspec argv, {connection_file}
        // substituted in). Mutually exclusive with --registration-port:
        // when this is set, main() below binds directly to the ports in
        // the file instead of doing the themisto-specific
        // dial-home handshake (see sendConnectionInfo below), since a
        // generic Jupyter frontend has no registration listener to dial.
        std::string connectionFile;
    };

    // Minimal `--flag value` parser -- this executable is only ever invoked
    // by themisto (native/src/themisto/kernel_process.cpp) or,
    // in --connection-file mode, a Jupyter frontend via this kernel's own
    // kernelspec (see kernelspec/kernel.json), never typed by a person, so
    // it doesn't need a full CLI library. -f is accepted as a synonym for
    // --connection-file specifically because that's the flag name Jupyter's
    // own kernelspec convention (`"argv": [..., "-f", "{connection_file}"]`)
    // uses.
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
        opts.rHome = get("r-home");
        opts.rPath = get("r-path");
        opts.rLibs = get("r-libs");
        opts.pandocPath = get("pandoc-path");
        opts.heraSrcPath = get("hera-src-path");
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
        std::cerr << "[elara] --registration-port and --key are required unless -f/--connection-file is "
                     "given (this executable is meant to be launched by themisto, or directly by a "
                     "Jupyter frontend via its kernelspec)"
                  << std::endl;
        return 1;
    }

    elara::EnvironmentConfig envConfig;
    envConfig.r_home = opts.rHome;
    envConfig.r_path = opts.rPath;
    envConfig.r_libs = opts.rLibs;
    envConfig.pandoc_path = opts.pandocPath;
    envConfig.hera_src_path = opts.heraSrcPath;

    adrastea::KernelConfiguration kernelConfig;

    if (standaloneJupyterMode)
    {
        // Standard Jupyter launch: a frontend (JupyterLab, `jupyter
        // console`, jupyter_client's KernelManager, ...) already picked
        // ports and a signing key and wrote them into this file per this
        // kernel's kernelspec (kernelspec/kernel.json's
        // `-f {connection_file}`) -- bind directly to those instead of
        // picking fresh ones (loadConfiguration()/KernelConfiguration
        // already existed for this, just never had a caller until now).
        adrastea::configuration parsed;
        try
        {
            parsed = adrastea::loadConfiguration(opts.connectionFile);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[elara] FATAL: failed to read connection file " << opts.connectionFile << ": "
                      << e.what() << std::endl;
            return 1;
        }
        if (!std::holds_alternative<adrastea::KernelConfiguration>(parsed))
        {
            std::cerr << "[elara] FATAL: " << opts.connectionFile
                      << " is a registration-style connection file (has registration_ip) -- that mode is for "
                         "themisto, launched via --registration-port/--key instead of -f."
                      << std::endl;
            return 1;
        }
        kernelConfig = std::get<adrastea::KernelConfiguration>(parsed);
    }
    else
    {
        kernelConfig = makeKernelConfiguration(opts.key);
    }

    elara::Server server(envConfig);

    // server.start() runs entirely on this thread and blocks until the
    // kernel shuts down (see elara::Server's class comment for why it's
    // no longer a background thread). The on_ready callback below runs
    // synchronously, once the kernel's ports are bound but before it
    // enters that blocking poll loop -- exactly the window the old
    // thread-plus-promise dance existed to expose to main(), just without
    // the thread. If the callback throws (e.g. the supervisor never ACKs
    // the registration handshake), Server::start() logs it and
    // rethrows without ever entering the blocking loop -- caught below.
    try
    {
        if (standaloneJupyterMode)
        {
            server.start(kernelConfig, [&]() {
                // No registration listener to dial in this mode -- the
                // connection file's ports are all a Jupyter frontend
                // needs; it discovers liveness the same way it does for
                // any other kernel (polling kernel_info_request until one
                // succeeds), not an explicit handshake.
                std::cerr << "[elara] ready (Jupyter connection-file mode), shell=" << kernelConfig.m_shellPort
                          << " control=" << kernelConfig.m_controlPort << " iopub=" << kernelConfig.m_iopubPort
                          << std::endl;
            });
        }
        else
        {
            server.start(kernelConfig, [&]() {
                // Registration: dial the supervisor's registration endpoint
                // with our now-bound ports, HMAC-signed with the shared
                // per-session key the supervisor generated and passed us
                // via --key. See sendConnectionInfo (handshaking.cpp) --
                // this call blocks until the supervisor ACKs.
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
                std::cerr << "[elara] registered with supervisor at "
                          << opts.registrationIp << ":" << opts.registrationPort << std::endl;
            });
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[elara] FATAL: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
