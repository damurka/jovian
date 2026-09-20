// themisto: the kernel supervisor.
//
// Spawns and supervises `elara` kernel processes, speaking ZMQ to
// each (reusing the existing client transport code, see session_registry.*)
// and re-exposing sessions over REST (http_api.*) + WebSocket (ws_relay.*)
// so that a Node/Electron consumer (lib/session/supervisor-client.ts) never
// needs a native ZMQ binding in-process.
//
// On startup this prints one JSON line to stdout announcing the bound HTTP
// and WS ports, then blocks until killed by its parent -- the same "wait
// for a ready line, then own the child's lifecycle" shape session-manager.ts
// already used for the old IPC-based worker (see the workerReady message it
// used to wait for), just via stdout instead of Node IPC since this process
// isn't a Node child anymore.
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>

#include "adrastea/json.hpp"

#include "http_api.hpp"
#include "session_registry.hpp"
#include "ws_relay.hpp"

namespace
{
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

    std::string siblingExePath(const char* argv0, const char* name)
    {
        std::filesystem::path selfDir = std::filesystem::absolute(argv0).parent_path();
#ifdef _WIN32
        return (selfDir / (std::string(name) + ".exe")).string();
#else
        return (selfDir / name).string();
#endif
    }
}

int main(int argc, char* argv[])
{
    auto args = parseArgs(argc, argv);

    std::string kernelExePath = args.count("kernel-exe") ? args["kernel-exe"] : siblingExePath(argv[0], "elara");
    std::string pythonKernelExePath = args.count("python-kernel-exe") ? args["python-kernel-exe"]
                                                                       : siblingExePath(argv[0], "carpo");
    std::string registrationIp = args.count("registration-ip") ? args["registration-ip"] : "127.0.0.1";

    if (!std::filesystem::exists(kernelExePath))
    {
        std::cerr << "[themisto] FATAL: kernel executable not found at " << kernelExePath
                  << " (pass --kernel-exe to override)" << std::endl;
        return 1;
    }

    // Unlike elara, carpo is optional (JOVIAN_BUILD_CARPO can be turned OFF, see
    // native/CMakeLists.txt) -- its absence doesn't stop this supervisor
    // from starting, it just means createSession() for kernelType "python"
    // fails with a clear error (SessionRegistry::createSessionWithId())
    // instead of every session, R included, being blocked on it.
    std::map<std::string, std::string> kernelExePaths = { { "r", kernelExePath } };
    if (std::filesystem::exists(pythonKernelExePath))
    {
        kernelExePaths["python"] = pythonKernelExePath;
    }
    else
    {
        std::cerr << "[themisto] NOTE: no Python kernel executable found at " << pythonKernelExePath
                  << " -- sessions with kernelType 'python' will fail to create until one is built "
                     "(JOVIAN_BUILD_CARPO) or --python-kernel-exe is passed." << std::endl;
    }

    themisto::SessionRegistry registry(kernelExePaths, registrationIp);
    registry.startRegistrationListener();

    themisto::HttpApi httpApi(registry);
    int httpPort = httpApi.start();

    themisto::WsRelay wsRelay(registry);
    int wsPort = wsRelay.start();

    adrastea::json ready = {
        { "type", "supervisorReady" },
        { "httpPort", httpPort },
        { "wsPort", wsPort }
    };
    std::cout << ready.dump() << std::endl;
    std::cout.flush();

    // Blocks forever -- this process is owned and terminated by its parent
    // (lib/session/session-manager.ts's SupervisorClient), same lifecycle
    // model as today's per-session worker processes.
    std::promise<void> forever;
    forever.get_future().wait();
    return 0;
}
