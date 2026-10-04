#include "carpo/engine.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace carpo
{
    void Server::setupEnvironment() {
        printf("[carpo::Server] setup_environment() called\n");
        fflush(stdout);

        if (!env_config.python_home.empty()) {
#ifdef _WIN32
            _putenv_s("PYTHONHOME", env_config.python_home.c_str());
#else
            setenv("PYTHONHOME", env_config.python_home.c_str(), 1);
#endif
            printf("[carpo::Server] Set PYTHONHOME=%s\n", env_config.python_home.c_str());
            fflush(stdout);
        }

        if (!env_config.python_path.empty()) {
#ifdef _WIN32
            _putenv_s("PYTHONPATH", env_config.python_path.c_str());
#else
            setenv("PYTHONPATH", env_config.python_path.c_str(), 1);
#endif
        }

        // Not a real Python env var -- CARPO_VENV_PATH is this codebase's
        // own signal, read by the bootstrap source (interpreter_py.cpp's
        // kBootstrapSource) to prepend the venv's site-packages directory to
        // sys.path. PYTHONHOME above still points at the *base* install
        // (set from env_config.python_home, not this venv) -- a venv has no
        // libpython/stdlib of its own for an embedded interpreter to load.
        if (!env_config.venv_path.empty()) {
#ifdef _WIN32
            _putenv_s("CARPO_VENV_PATH", env_config.venv_path.c_str());
#else
            setenv("CARPO_VENV_PATH", env_config.venv_path.c_str(), 1);
#endif
            printf("[carpo::Server] Set CARPO_VENV_PATH=%s\n", env_config.venv_path.c_str());
            fflush(stdout);
        }

        printf("[carpo::Server] setup_environment() completed\n");
        fflush(stdout);
    }

    void Server::start(const adrastea::KernelConfiguration& config, std::function<void(const adrastea::KernelConfiguration&)> on_ready) {
        bool on_ready_called = false;
        // on_ready gets the configuration with the ports the kernel's sockets are bound to. Started by the
        // supervisor, the configuration names no ports: each socket binds a free one itself, here, and those are
        // reported. (Probing five ports first and binding them later left a gap in which another process took one.)
        auto call_on_ready_once = [&](const adrastea::KernelConfiguration& bound) {
            if (on_ready && !on_ready_called) {
                on_ready_called = true;
                on_ready(bound);
            }
        };

        try {
            this->setupEnvironment();

            auto context = adrastea::makeZmqContext();
            char* py_argv[] = { (char*)"carpo" };
            int py_argc = sizeof(py_argv) / sizeof(py_argv[0]);

            using interpreter_ptr = std::unique_ptr<PyInterpreter>;
            interpreter_ptr interpreter = interpreter_ptr(new PyInterpreter(py_argc, py_argv));

            auto history = adrastea::makeInMemoryHistoryManager();
            auto logger = adrastea::makeConsoleLogger(adrastea::Logger::level::msg_type);

            adrastea::Kernel engine(config, adrastea::getUserName(), std::move(context), std::move(interpreter), adrastea::makeServerDefault, std::move(history), std::move(logger));

            call_on_ready_once(engine.getConfig());

            engine.start();
        }
        catch (const std::exception& e) {
            std::cerr << "[Server] FATAL ERROR: " << e.what() << std::endl;
            if (!on_ready_called) {
                throw;
            }
        }
    }
}
