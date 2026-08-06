// Standalone Jupyter-protocol R kernel executable ("datasuite-r").
//
// Reuses DatasuiteServer (native/src/bridge/datasuite_engine.cpp) verbatim
// for environment setup and booting the embedded R Kernel on a background
// thread -- that class has no N-API dependency (only bridge/addon.cpp does),
// so it's shared between this executable and the Node addon rather than
// duplicated. What this file adds on top is the piece the addon never
// needed: registering with a supervisor process over ZMQ so the supervisor
// (not an in-process DatasuiteClient) becomes this kernel's client. See
// native/src/transport/server/handshaking.cpp's sendConnectionInfo, which
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

#include "datasuite/context.hpp"
#include "datasuite/datasuite_engine.hpp"
#include "datasuite/kernel_configuration.hpp"
#include "datasuite/message.hpp"
#include "datasuite/middleware.hpp"

#include "transport/common/authentication.hpp"
#include "transport/server/handshaking.hpp"

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
    };

    // Minimal `--flag value` parser -- this executable is only ever invoked
    // by datasuite-supervisor (native/src/supervisor/kernel_process.cpp),
    // never typed by a person, so it doesn't need a full CLI library.
    std::unordered_map<std::string, std::string> parseArgs(int argc, char* argv[])
    {
        std::unordered_map<std::string, std::string> args;
        for (int i = 1; i + 1 < argc; i += 2)
        {
            std::string flag = argv[i];
            if (flag.rfind("--", 0) == 0)
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
        return opts;
    }

    datasuite::KernelConfiguration makeKernelConfiguration(const std::string& key)
    {
        datasuite::KernelConfiguration config;
        config.m_transport = "tcp";
        config.m_ip = "127.0.0.1";
        config.m_shellPort = datasuite::findFreePort();
        config.m_controlPort = datasuite::findFreePort();
        config.m_stdinPort = datasuite::findFreePort();
        config.m_iopubPort = datasuite::findFreePort();
        config.m_hbPort = datasuite::findFreePort();
        config.m_signatureScheme = "hmac-sha256";
        config.m_key = key;
        return config;
    }
}

int main(int argc, char* argv[])
{
    CliOptions opts = toCliOptions(parseArgs(argc, argv));

    if (opts.registrationPort.empty() || opts.key.empty())
    {
        std::cerr << "[datasuite-r] --registration-port and --key are required "
                     "(this executable is meant to be launched by datasuite-supervisor)"
                  << std::endl;
        return 1;
    }

    datasuite::EnvironmentConfig envConfig;
    envConfig.r_home = opts.rHome;
    envConfig.r_path = opts.rPath;
    envConfig.r_libs = opts.rLibs;
    envConfig.pandoc_path = opts.pandocPath;
    envConfig.hera_src_path = opts.heraSrcPath;

    datasuite::KernelConfiguration kernelConfig = makeKernelConfiguration(opts.key);

    datasuite::DatasuiteServer server(envConfig);

    std::promise<void> readyPromise;
    std::future<void> readyFuture = readyPromise.get_future();
    server.start(kernelConfig, [&readyPromise]() { readyPromise.set_value(); });
    readyFuture.wait();

    // Registration: dial the supervisor's registration endpoint with our
    // now-bound ports, HMAC-signed with the shared per-session key the
    // supervisor generated and passed us via --key. See sendConnectionInfo
    // (handshaking.cpp) -- this call blocks until the supervisor ACKs.
    try
    {
        auto handshakeContext = datasuite::makeZmqContext();
        auto& zmqContext = handshakeContext->getWrappedContext<zmq::context_t>();

        datasuite::RegistrationConfiguration regConfig;
        regConfig.m_transport = "tcp";
        regConfig.m_signatureScheme = "hmac-sha256";
        regConfig.m_key = opts.key;
        regConfig.m_registrationIp = opts.registrationIp;
        regConfig.m_registrationPort = opts.registrationPort;

        auto auth = datasuite::makeAuthentication("hmac-sha256", opts.key);

        datasuite::sendConnectionInfo(zmqContext, regConfig, kernelConfig, *auth, datasuite::json::error_handler_t::strict);
        std::cerr << "[datasuite-r] registered with supervisor at "
                  << opts.registrationIp << ":" << opts.registrationPort << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[datasuite-r] FATAL: failed to register with supervisor: " << e.what() << std::endl;
        return 1;
    }

    // Blocks (joins the server thread) until a shutdown_request arrives on
    // the control channel and the embedded Kernel::start() loop returns.
    server.stop();
    return 0;
}
