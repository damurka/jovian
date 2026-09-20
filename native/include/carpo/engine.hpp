#ifndef CARPO_ENGINE_HPP
#define CARPO_ENGINE_HPP

#include <functional>
#include <string>

#include "adrastea/adrastea.hpp"
#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/kernel.hpp"
#include "adrastea/server_zmq.hpp"
#include "interpreter_py.hpp"
#include "adrastea/logger.hpp"
#include "adrastea/history_manager.hpp"

namespace carpo
{
    // Mirrors elara::EnvironmentConfig (native/include/elara/engine.hpp) --
    // Python's equivalent of R_HOME/R_LIBS. python_home is set as PYTHONHOME
    // (Server::setupEnvironment(), bridge/engine.cpp) before PyInterpreter's
    // constructor loads Python's shared library dynamically at runtime (see
    // native/src/carpo/py/py_dynlib.hpp, mirroring native/src/elara/r/
    // r_dynlib.hpp). venv_path is not consulted yet -- a later pass's job.
    struct ADRASTEA_API EnvironmentConfig
    {
        std::string python_home;
        std::string python_path;
        std::string venv_path;
    };

    // Mirrors elara::Server (native/include/elara/engine.hpp) exactly --
    // see that class's comment for why start() runs synchronously on the
    // calling thread rather than spawning a background one.
    class ADRASTEA_API Server
    {
    public:
        Server() = default;
        explicit Server(const EnvironmentConfig& env) : env_config(env) {}

        void start(const adrastea::KernelConfiguration& config, std::function<void()> on_ready = nullptr);

    private:
        void setupEnvironment();

        EnvironmentConfig env_config;
    };
}

#endif // CARPO_ENGINE_HPP
