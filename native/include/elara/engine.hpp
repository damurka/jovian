#ifndef ELARA_ENGINE_HPP
#define ELARA_ENGINE_HPP

#include <iostream>
#include <memory>
#include <string>
#include <functional>

#include "adrastea/adrastea.hpp"
#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/kernel.hpp"
#include "adrastea/server_zmq.hpp"
#include "interpreter_r.hpp"
#include "adrastea/logger.hpp"
#include "adrastea/history_manager.hpp"

namespace elara
{

    // =========================================================================
    // ENVIRONMENT CONFIGURATION
    // =========================================================================
    struct ADRASTEA_API EnvironmentConfig {
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
    // THE SERVER: boots an embedded R Kernel and runs it to completion, on
    // the calling thread. This is the piece the standalone kernel
    // executable (native/src/elara.cpp) uses directly -- it has no
    // dependency on how the resulting Kernel gets talked to (formerly an
    // in-process Client/Engine facade for the Node addon;
    // now a separate themisto process over ZMQ, see
    // native/src/supervisor/session_registry.cpp).
    //
    // start() runs synchronously on whichever thread calls it, deliberately
    // -- this used to spawn R onto a background thread (with a deliberately
    // enlarged stack to compensate for what that broke, see below), a
    // carryover from when this code ran as a Node addon loaded into a Node
    // process, where a background thread was genuinely necessary to keep
    // Node's own event loop free. That constraint doesn't apply to a
    // standalone executable, and running on a background thread had a real
    // cost: R's own C-stack-bounds auto-detection assumes it's running on
    // the process's actual main thread, and silently computes wrong bounds
    // when it isn't -- this is what caused a real, reproduced crash
    // (STATUS_STACK_OVERFLOW during a live Shiny session) when that
    // auto-detected limit was disabled outright to work around the wrong
    // values. Running directly on the caller's thread -- exactly how
    // xeus-r's main() constructs its interpreter and calls kernel.start()
    // with no thread of its own -- removes the problem instead of
    // compensating for it: R's own auto-detection just works, and
    // RInterpreter no longer needs to query/override it at all (see
    // interpreter_r.cpp).
    // =========================================================================
    class ADRASTEA_API Server {
    public:
        Server() = default;
        explicit Server(const EnvironmentConfig& env) : env_config(env) {}

        // Blocks until the kernel receives a shutdown_request and its poll
        // loop returns. `on_ready` is called once the kernel's ports are
        // bound, just before the blocking poll loop starts -- synchronously,
        // on this same thread, so the caller can do its own setup (e.g. a
        // registration handshake) between "ports are live" and "kernel is
        // now consuming its own thread until shutdown".
        void start(const adrastea::KernelConfiguration& config, std::function<void()> on_ready = nullptr);

    private:
        void setupEnvironment();

        EnvironmentConfig env_config;
    };
}

#endif // ELARA_ENGINE_HPP
