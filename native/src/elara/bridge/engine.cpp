#include "elara/engine.hpp"

namespace elara
{
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
            // LD_LIBRARY_PATH/DYLD_LIBRARY_PATH are deliberately NOT set here
            // (a first attempt at that lived in this function briefly, and
            // didn't work -- confirmed via real CI evidence: the dynamic
            // linker builds its search path once at process startup, before
            // main() runs, so a setenv() from within this already-running
            // process has no effect on it). That fix now lives in
            // KernelProcess::start() (kernel_process.cpp), which sets it in
            // the child between fork() and exec() -- early enough for the
            // new process image's own linker startup to actually see it. See
            // that function's comment for the full story of what this fixes.
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
    void Server::start(const adrastea::KernelConfiguration& config, std::function<void()> on_ready) {
        bool on_ready_called = false;
        auto call_on_ready_once = [&]() {
            if (on_ready && !on_ready_called) {
                on_ready_called = true;
                on_ready();
            }
        };

        try {
            // CRITICAL: Setup R environment BEFORE initializing R interpreter!
            this->setupEnvironment();

            auto context = adrastea::makeZmqContext();
            // Don't use --vanilla, it prevents loading default packages
            char* r_argv[] = { (char*)"R", (char*)"--quiet", (char*)"--no-save", (char*)"--no-restore" };
            int r_argc = sizeof(r_argv) / sizeof(r_argv[0]);

            // RInterpreter's constructor is where a missing/unloadable R
            // installation now surfaces (r::loadRApi(), interpreter_r.cpp)
            // as an ordinary std::runtime_error -- caught below.
            using interpreter_ptr = std::unique_ptr<RInterpreter>;
            interpreter_ptr interpreter = interpreter_ptr(new RInterpreter(r_argc, r_argv));

            auto history = adrastea::makeInMemoryHistoryManager();
            auto logger = adrastea::makeConsoleLogger(adrastea::Logger::level::msg_type);

            adrastea::Kernel engine(config, adrastea::getUserName(), std::move(context), std::move(interpreter), adrastea::makeServerDefault, std::move(history), std::move(logger));

            call_on_ready_once();

            // Blocks indefinitely until the Client sends a shutdown_request
            engine.start();
        }
        catch (const std::exception& e) {
            std::cerr << "[Server] FATAL ERROR: " << e.what() << std::endl;
            if (!on_ready_called) {
                // Nothing was ever reported ready -- rethrow so the caller
                // (elara.cpp's main(), which already catches and reports
                // fatal startup errors with a non-zero exit code) sees a
                // real failure instead of on_ready() misleadingly signaling
                // success (e.g. completing the supervisor registration
                // handshake, or printing "ready") for a kernel that never
                // actually started. This is the common path for a missing
                // R installation now that R.dll loads dynamically (see
                // RInterpreter's ctor) instead of preventing the process
                // from starting at all.
                throw;
            }
            // on_ready already ran once (the client already believes this
            // kernel is live) -- nothing to un-notify; keep going rather
            // than also reporting a second, confusing failure.
        }
    }

}
