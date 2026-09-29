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
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <future>
#include <iostream>
#include <csignal>
#include <map>
#include <string>
#include <unordered_map>

#include "adrastea/json.hpp"

#include "access.hpp"
#include "activity.hpp"
#include "http_api.hpp"
#include "session_registry.hpp"
#include "ws_relay.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    std::unordered_map<std::string, std::string> parseArgs(int argc, char* argv[])
    {
        // "--name value", or "--name" alone (a switch, value "").
        std::unordered_map<std::string, std::string> args;
        for (int i = 1; i < argc; ++i)
        {
            std::string flag = argv[i];
            if (flag.rfind("--", 0) != 0) continue;
            bool hasValue = i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0;
            args[flag.substr(2)] = hasValue ? argv[++i] : "";
        }
        return args;
    }

    long currentProcessId()
    {
#ifdef _WIN32
        return static_cast<long>(GetCurrentProcessId());
#else
        return static_cast<long>(getpid());
#endif
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
#ifndef _WIN32
    // A supervisor that outlives the client that started it (persistent
    // mode) keeps writing kernel output to a pipe nobody reads any more:
    // that must fail quietly, not kill the process.
    std::signal(SIGPIPE, SIG_IGN);
#endif
    auto args = parseArgs(argc, argv);

    std::string kernelExePath = args.count("kernel-exe") ? args["kernel-exe"] : siblingExePath(argv[0], "elara");
    std::string pythonKernelExePath = args.count("python-kernel-exe") ? args["python-kernel-exe"]
                                                                       : siblingExePath(argv[0], "carpo");
    std::string stataKernelExePath = args.count("stata-kernel-exe") ? args["stata-kernel-exe"]
                                                                     : siblingExePath(argv[0], "callisto");
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
    // Optional in the same way as carpo.
    if (std::filesystem::exists(stataKernelExePath))
    {
        kernelExePaths["stata"] = stataKernelExePath;
    }
    else
    {
        std::cerr << "[themisto] NOTE: no Stata kernel executable found at " << stataKernelExePath
                  << " -- sessions with kernelType 'stata' will fail to create until one is built "
                     "(JOVIAN_BUILD_CALLISTO) or --stata-kernel-exe is passed." << std::endl;
    }

    themisto::SessionRegistry registry(kernelExePaths, registrationIp);
    registry.startRegistrationListener();

    // The token every client request must carry (access.hpp). The parent
    // may choose it (JOVIAN_SUPERVISOR_TOKEN: in the environment, which other
    // users cannot read, unlike the command line) -- a client reconnecting to
    // a supervisor it started earlier needs to know it in advance -- else a
    // new random one. --no-auth turns the check off (tests only).
    std::string token;
    if (!args.count("no-auth"))
    {
        const char* fromEnv = std::getenv("JOVIAN_SUPERVISOR_TOKEN");
        token = fromEnv && *fromEnv ? std::string(fromEnv) : themisto::access::newToken();
    }

    // --idle-shutdown-minutes N: exit (stopping every session) once no client
    // has been connected, or made a request, for N minutes. For a supervisor
    // left running so a client can reconnect (a persistent SessionManager).
    long idleShutdownMinutes = args.count("idle-shutdown-minutes") ? std::stol(args["idle-shutdown-minutes"]) : 0;

    themisto::Activity activity;

    themisto::HttpApi httpApi(registry, token, activity);
    std::mutex shutdownMutex;
    std::condition_variable shutdownCv;
    bool shutdownRequested = false;
    httpApi.onShutdownRequested([&]() {
        {
            std::lock_guard<std::mutex> lock(shutdownMutex);
            shutdownRequested = true;
        }
        shutdownCv.notify_all();
    });
    int httpPort = httpApi.start();

    themisto::WsRelay wsRelay(registry, token, activity);
    int wsPort = wsRelay.start();

    adrastea::json ready = {
        { "type", "supervisorReady" },
        { "httpPort", httpPort },
        { "wsPort", wsPort },
        { "token", token },
        { "pid", currentProcessId() }
    };
    std::cout << ready.dump() << std::endl;
    std::cout.flush();

    // Runs until a client asks it to stop (POST /shutdown), it has been idle
    // for --idle-shutdown-minutes, or its parent kills it (the usual end:
    // lib/session/supervisor-client.ts owns this process).
    {
        std::unique_lock<std::mutex> lock(shutdownMutex);
        for (;;)
        {
            shutdownCv.wait_for(lock, std::chrono::seconds(5), [&] { return shutdownRequested; });
            if (shutdownRequested) break;
            if (idleShutdownMinutes > 0 && activity.openConnections() <= 0 &&
                activity.idleMs() >= idleShutdownMinutes * 60 * 1000)
            {
                std::cerr << "[themisto] idle for " << idleShutdownMinutes << " minutes with no client; shutting down" << std::endl;
                break;
            }
        }
    }

    // Each kernel gets a real shutdown_request, as stopSession() always does.
    for (const auto& session : registry.listSessions())
    {
        registry.stopSession(session.value("sessionId", ""));
    }
    wsRelay.stop();
    httpApi.stop();
    return 0;
}
