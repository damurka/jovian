#ifndef DATASUITE_ENGINE_HPP
#define DATASUITE_ENGINE_HPP

#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

#include "datasuite.hpp"
#include "context.hpp"
#include "kernel_configuration.hpp"
#include "kernel.hpp"
#include "server_zmq.hpp"
#include "interpreter_r.hpp"
#include "logger.hpp"
#include "history_manager.hpp"

namespace datasuite
{

    // =========================================================================
    // ENVIRONMENT CONFIGURATION
    // =========================================================================
    struct DATASUITE_API EnvironmentConfig {
        std::string r_home;
        std::string r_path;
        std::string r_libs;
        // Directory containing the pandoc binary. Bundled/portable R
        // installations often don't ship pandoc on PATH, which breaks
        // rmarkdown/officedown/flextable-based rendering unless this (or
        // RSTUDIO_PANDOC) points at one explicitly.
        std::string pandoc_path;
        // Source directory of the bundled 'hera' R package, used to
        // auto-install it via remotes::install_local() if it isn't already
        // present on r_libs. See RInterpreter::configureImpl().
        std::string hera_src_path;
    };

    // =========================================================================
    // THE SERVER: boots an embedded R Kernel on a background thread. This is
    // the piece the standalone kernel executable (native/src/datasuite-r.cpp)
    // reuses as-is -- it has no dependency on how the resulting Kernel gets
    // talked to (formerly an in-process DatasuiteClient/DatasuiteEngine
    // facade for the Node addon; now a separate datasuite-supervisor process
    // over ZMQ, see native/src/supervisor/session_registry.cpp).
    // =========================================================================
    class DATASUITE_API DatasuiteServer {
    private:
        std::thread server_thread;
        EnvironmentConfig env_config;

    public:
        DatasuiteServer() = default;
        explicit DatasuiteServer(const EnvironmentConfig& env) : env_config(env) {}
        ~DatasuiteServer() { stop(); }

        void start(const KernelConfiguration& config, std::function<void()> on_ready = nullptr);
        void stop();
    private:
        void setupEnvironment();
    };
}

#endif // DATASUITE_ENGINE_HPP
