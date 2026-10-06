#include "callisto/engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "adrastea/context.hpp"
#include "adrastea/history_manager.hpp"
#include "adrastea/kernel.hpp"
#include "adrastea/logger.hpp"
#include "adrastea/server_zmq.hpp"
#include "callisto/interpreter_stata.hpp"

namespace callisto
{
    namespace
    {
        void setEnv(const char* name, const std::string& value)
        {
#ifdef _WIN32
            _putenv_s(name, value.c_str());
#else
            setenv(name, value.c_str(), 1);
#endif
        }
    }

    // StataInterpreter's constructor reads these two, the way PyInterpreter
    // reads PYTHONHOME. STATA_HOME is the variable the TypeScript library
    // discovers Stata from too; CALLISTO_STATA_EDITION is only this kernel's.
    void Server::setupEnvironment() {
        if (!env_config.stata_home.empty()) {
            setEnv("STATA_HOME", env_config.stata_home);
            printf("[callisto::Server] Set STATA_HOME=%s\n", env_config.stata_home.c_str());
            fflush(stdout);
        }
        if (!env_config.stata_edition.empty()) {
            setEnv("CALLISTO_STATA_EDITION", env_config.stata_edition);
        }
    }

    void Server::start(const adrastea::KernelConfiguration& config, std::function<void(const adrastea::KernelConfiguration&)> on_ready) {
        bool on_ready_called = false;
        // on_ready failing (the supervisor refused the registration) is fatal: this kernel has no client
        bool on_ready_failed = false;
        // on_ready gets the configuration with the ports the kernel's sockets are bound to. Started by the
        // supervisor, the configuration names no ports: each socket binds a free one itself, here, and those are
        // reported. (Probing five ports first and binding them later left a gap in which another process took one.)
        auto call_on_ready_once = [&](const adrastea::KernelConfiguration& bound) {
            if (on_ready && !on_ready_called) {
                on_ready_called = true;
                try {
                    on_ready(bound);
                }
                catch (...) {
                    on_ready_failed = true;
                    throw;
                }
            }
        };

        try {
            this->setupEnvironment();

            auto context = adrastea::makeZmqContext();
            auto interpreter = std::make_unique<StataInterpreter>();

            auto history = adrastea::makeInMemoryHistoryManager();
            auto logger = adrastea::makeConsoleLogger(adrastea::Logger::level::msg_type);

            adrastea::Kernel engine(config, adrastea::getUserName(), std::move(context), std::move(interpreter), adrastea::makeServerDefault, std::move(history), std::move(logger));

            call_on_ready_once(engine.getConfig());

            engine.start();
        }
        catch (const std::exception& e) {
            std::cerr << "[Server] FATAL ERROR: " << e.what() << std::endl;
            if (!on_ready_called || on_ready_failed) {
                throw;
            }
        }
    }
}
