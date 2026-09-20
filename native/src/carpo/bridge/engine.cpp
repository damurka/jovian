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

        printf("[carpo::Server] setup_environment() completed -- carpo is scaffolding only, "
               "no real Python is embedded yet\n");
        fflush(stdout);
    }

    void Server::start(const adrastea::KernelConfiguration& config, std::function<void()> on_ready) {
        bool on_ready_called = false;
        auto call_on_ready_once = [&]() {
            if (on_ready && !on_ready_called) {
                on_ready_called = true;
                on_ready();
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

            call_on_ready_once();

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
