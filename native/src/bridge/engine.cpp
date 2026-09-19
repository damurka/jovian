#include "elara/engine.hpp"

namespace elara
{
    // Elara builds on the Adrastea framework; name its symbols unqualified here.
    using namespace adrastea;

    // =========================================================================
    // ELARA SERVER IMPLEMENTATION
    // =========================================================================
    void Server::setupEnvironment() {
        printf("[elara::Server] setup_environment() called\n");
        fflush(stdout);

        // Set R_HOME
        if (!env_config.r_home.empty()) {
            #ifdef _WIN32
            // Convert forward slashes to backslashes for Windows
            std::string r_home_win = env_config.r_home;
            std::replace(r_home_win.begin(), r_home_win.end(), '/', '\\');
            _putenv_s("R_HOME", r_home_win.c_str());
            printf("[elara::Server] Set R_HOME=%s\n", r_home_win.c_str());
            fflush(stdout);
            #else
            setenv("R_HOME", env_config.r_home.c_str(), 1);
            #endif
        } else {
            printf("[elara::Server] WARNING: R_HOME is empty!\n");
            fflush(stdout);
        }

        // Add R bin path to PATH
        if (!env_config.r_path.empty()) {
            #ifdef _WIN32
            std::string r_path_win = env_config.r_path;
            std::replace(r_path_win.begin(), r_path_win.end(), '/', '\\');
            std::string current_path = getenv("PATH") ? getenv("PATH") : "";
            std::string new_path = r_path_win + ";" + current_path;
            _putenv_s("PATH", new_path.c_str());
            printf("[elara::Server] Added to PATH=%s\n", r_path_win.c_str());
            fflush(stdout);
            #else
            std::string current_path = getenv("PATH") ? getenv("PATH") : "";
            std::string new_path = env_config.r_path + ":" + current_path;
            setenv("PATH", new_path.c_str(), 1);
            #endif
        }

        // Set R_LIBS - this controls where R looks for packages
        if (!env_config.r_libs.empty()) {
            #ifdef _WIN32
            std::string r_libs_win = env_config.r_libs;
            std::replace(r_libs_win.begin(), r_libs_win.end(), '/', '\\');
            _putenv_s("R_LIBS", r_libs_win.c_str());
            _putenv_s("R_LIBS_USER", r_libs_win.c_str());
            _putenv_s("R_LIBS_SITE", r_libs_win.c_str());
            printf("[elara::Server] Set R_LIBS=%s\n", r_libs_win.c_str());
            printf("[elara::Server] Set R_LIBS_USER=%s\n", r_libs_win.c_str());
            printf("[elara::Server] Set R_LIBS_SITE=%s\n", r_libs_win.c_str());
            fflush(stdout);
            #else
            setenv("R_LIBS", env_config.r_libs.c_str(), 1);
            setenv("R_LIBS_USER", env_config.r_libs.c_str(), 1);
            #endif
        }

        // Set pandoc's location - RSTUDIO_PANDOC is the variable
        // rmarkdown::find_pandoc() (used by rmarkdown/officedown/flextable
        // rendering) checks first, so bundled R installs that don't ship
        // pandoc on PATH can still render without a system-wide install.
        if (!env_config.pandoc_path.empty()) {
            #ifdef _WIN32
            std::string pandoc_path_win = env_config.pandoc_path;
            std::replace(pandoc_path_win.begin(), pandoc_path_win.end(), '/', '\\');
            _putenv_s("RSTUDIO_PANDOC", pandoc_path_win.c_str());
            std::string current_path = getenv("PATH") ? getenv("PATH") : "";
            std::string new_path = pandoc_path_win + ";" + current_path;
            _putenv_s("PATH", new_path.c_str());
            printf("[elara::Server] Set RSTUDIO_PANDOC=%s\n", pandoc_path_win.c_str());
            fflush(stdout);
            #else
            setenv("RSTUDIO_PANDOC", env_config.pandoc_path.c_str(), 1);
            std::string current_path = getenv("PATH") ? getenv("PATH") : "";
            std::string new_path = env_config.pandoc_path + ":" + current_path;
            setenv("PATH", new_path.c_str(), 1);
            #endif
        }

        // Point RInterpreter::configureImpl() at the bundled 'hera' source
        // so it can auto-install it into r_libs if it's missing.
        if (!env_config.hera_src_path.empty()) {
            #ifdef _WIN32
            std::string hera_src_win = env_config.hera_src_path;
            std::replace(hera_src_win.begin(), hera_src_win.end(), '/', '\\');
            _putenv_s("ELARA_HERA_SRC", hera_src_win.c_str());
            printf("[elara::Server] Set ELARA_HERA_SRC=%s\n", hera_src_win.c_str());
            fflush(stdout);
            #else
            setenv("ELARA_HERA_SRC", env_config.hera_src_path.c_str(), 1);
            #endif
        }

        printf("[elara::Server] setup_environment() completed\n");
        fflush(stdout);
    }

    // Runs entirely on the calling thread -- see the class comment
    // (engine.hpp) for why that's deliberate. Blocks until the
    // kernel shuts down; the caller (elara.cpp's main()) has nothing
    // left to do afterward but return.
    void Server::start(const KernelConfiguration& config, std::function<void()> on_ready) {
        try {
            // CRITICAL: Setup R environment BEFORE initializing R interpreter!
            this->setupEnvironment();

            auto context = makeZmqContext();
            // Don't use --vanilla, it prevents loading default packages
            char* r_argv[] = { (char*)"R", (char*)"--quiet", (char*)"--no-save", (char*)"--no-restore" };
            int r_argc = sizeof(r_argv) / sizeof(r_argv[0]);

            using interpreter_ptr = std::unique_ptr<RInterpreter>;
            interpreter_ptr interpreter = interpreter_ptr(new RInterpreter(r_argc, r_argv));

            auto history = makeInMemoryHistoryManager();
            auto logger = makeConsoleLogger(Logger::level::msg_type);

            Kernel engine(config, getUserName(), std::move(context), std::move(interpreter), makeServerDefault, std::move(history), std::move(logger));

            if (on_ready) {
                on_ready();
            }

            // Blocks indefinitely until the Client sends a shutdown_request
            engine.start();
        }
        catch (const std::exception& e) {
            std::cerr << "[Server] FATAL ERROR: " << e.what() << std::endl;
            if (on_ready) {
                on_ready();
            }
        }
    }

}
