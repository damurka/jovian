#include "datasuite/datasuite_engine.hpp"
#include "datasuite/middleware.hpp"

namespace datasuite
{

    // =========================================================================
    // DATASUITE SERVER IMPLEMENTATION
    // =========================================================================
    void DatasuiteServer::setup_environment() {
        printf("[DatasuiteServer] setup_environment() called\n");
        fflush(stdout);

        // Set R_HOME
        if (!env_config.r_home.empty()) {
            #ifdef _WIN32
            // Convert forward slashes to backslashes for Windows
            std::string r_home_win = env_config.r_home;
            std::replace(r_home_win.begin(), r_home_win.end(), '/', '\\');
            _putenv_s("R_HOME", r_home_win.c_str());
            printf("[DatasuiteServer] Set R_HOME=%s\n", r_home_win.c_str());
            fflush(stdout);
            #else
            setenv("R_HOME", env_config.r_home.c_str(), 1);
            #endif
        } else {
            printf("[DatasuiteServer] WARNING: R_HOME is empty!\n");
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
            printf("[DatasuiteServer] Added to PATH=%s\n", r_path_win.c_str());
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
            printf("[DatasuiteServer] Set R_LIBS=%s\n", r_libs_win.c_str());
            printf("[DatasuiteServer] Set R_LIBS_USER=%s\n", r_libs_win.c_str());
            printf("[DatasuiteServer] Set R_LIBS_SITE=%s\n", r_libs_win.c_str());
            fflush(stdout);
            #else
            setenv("R_LIBS", env_config.r_libs.c_str(), 1);
            setenv("R_LIBS_USER", env_config.r_libs.c_str(), 1);
            #endif
        }

        printf("[DatasuiteServer] setup_environment() completed\n");
        fflush(stdout);
    }

    void DatasuiteServer::start(const KernelConfiguration& config, std::function<void()> on_ready) {
        server_thread = std::thread([this, config, on_ready]() {
            try {
                // CRITICAL: Setup R environment BEFORE initializing R interpreter!
                this->setup_environment();

                auto context = make_zmq_context();
                // Don't use --vanilla, it prevents loading default packages
                char* r_argv[] = { (char*)"R", (char*)"--quiet", (char*)"--no-save", (char*)"--no-restore" };
                int r_argc = sizeof(r_argv) / sizeof(r_argv[0]);

                using interpreter_ptr = std::unique_ptr<RInterpreter>;
                interpreter_ptr interpreter = interpreter_ptr(new RInterpreter(r_argc, r_argv));

                auto history = make_in_memory_history_manager();
                auto logger = make_console_logger(Logger::level::msg_type);

                Kernel engine(config, get_user_name(), std::move(context), std::move(interpreter), make_server_default, std::move(history), std::move(logger));

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
        });
    }

    void DatasuiteServer::stop() {
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }

    // =========================================================================
    // DATASUITE CLIENT IMPLEMENTATION
    // =========================================================================
    void DatasuiteClient::start(const KernelConfiguration& config) {
        client_context = make_zmq_context();
        zmq_client = make_client_zmq(*client_context, config);
        zmq_client->connect();
        zmq_client->start();
    }

    std::string DatasuiteClient::execute(const std::string& code) {
        if (!zmq_client) return std::string();

        json header = make_header("execute_request", "client_user", "session_1");
        std::string msg_id = header.value("msg_id", "");
        json content = {
            {"code", code},
            {"silent", false},
            {"store_history", true},
            {"user_expressions", json::object()},
            {"allow_stdin", false}
        };

        Message req({ "client_id" }, header, json::object(), json::object(), content, buffer_sequence());
        zmq_client->send_on_shell(std::move(req));

        return msg_id;
    }

    void DatasuiteClient::stop() {
        if (!zmq_client) return;

        json shut_header = make_header("shutdown_request", "client_user", "session_1");
        json shut_content = { {"restart", false} };
        Message shut_req({ "client_id" }, shut_header, json::object(), json::object(), shut_content, buffer_sequence());

        // Send shutdown command on the Control channel
        zmq_client->send_on_control(std::move(shut_req));
        zmq_client->stop_channels();
    }

    // =========================================================================
    // DATASUITE ENGINE IMPLEMENTATION (The Node.js Bridge)
    // =========================================================================
    namespace
    {
        // Each DatasuiteEngine embeds its own R interpreter in this process
        // and binds its own set of ZMQ ports, so a second concurrently
        // running engine (e.g. a second R session) must not collide with
        // the first on any of them. find_free_port() (middleware.hpp) asks
        // the OS for an available ephemeral port instead of hardcoding one.
        KernelConfiguration make_localhost_configuration()
        {
            KernelConfiguration config;
            config.m_transport = "tcp";
            config.m_ip = "127.0.0.1";
            config.m_shellPort = find_free_port();
            config.m_controlPort = find_free_port();
            config.m_stdinPort = find_free_port();
            config.m_iopubPort = find_free_port();
            config.m_hbPort = find_free_port();
            config.m_signatureScheme = "hmac-sha256";
            config.m_key = "shared-secret-key";
            return config;
        }
    }

    DatasuiteEngine::DatasuiteEngine() : config(make_localhost_configuration()), server() {
    }

    DatasuiteEngine::DatasuiteEngine(const EnvironmentConfig& env)
        : config(make_localhost_configuration()), env_config(env), server(env) {
    }

    void DatasuiteEngine::init() {
        if (is_initialized) return;

        std::promise<void> server_ready_promise;
        std::future<void> server_ready_future = server_ready_promise.get_future();

        // 1. Boot the Server in a background thread
        server.start(config, [&server_ready_promise]() {
            server_ready_promise.set_value();
        });

        server_ready_future.wait();

        // 2. Connect the Client
        client.start(config);

        is_initialized = true;
    }

    void DatasuiteEngine::start(std::function<void(std::string)> callback) {
        if (!is_initialized) {
            init();
        }

        if (is_running) return;

        on_message_callback = callback;
        is_running = true;

        // 3. Start the Background Polling Thread
        polling_thread = std::thread(&DatasuiteEngine::poll_messages, this);
    }

    std::string DatasuiteEngine::execute(const std::string& code) {
        if (!is_running) return std::string();
        return client.execute(code);
    }

    void DatasuiteEngine::stop() {
        if (!is_running) return;
        is_running = false;

        // 1. Stop the polling thread
        if (polling_thread.joinable()) {
            polling_thread.join();
        }

        // 2. Stop the client (This sends the shutdown signal to the Server)
        client.stop();

        // 3. Join the server thread (Waits for embedded R to exit cleanly)
        server.stop();
    }

    void DatasuiteEngine::poll_messages() {
        auto* zmq = client.get_zmq_client();

        while (is_running) {
            // Drain IOPUB messages (print statements, images, results)
            while (zmq->iopub_queue_size() > 0) {
                if (auto pub_opt = zmq->pop_iopub_message()) {
                    auto& msg = pub_opt.value();

                    json envelope = {
                        {"channel", "iopub"},
                        {"topic", msg.topic()},
                        {"msg_type", msg.header().value("msg_type", "")},
                        {"parent_msg_id", msg.parent_header().value("msg_id", "")},
                        {"content", msg.content()}
                    };
                    on_message_callback(envelope.dump());
                }
            }

            // Check Shell messages (Execution Reply / Errors)
            if (auto shell_opt = zmq->receive_on_shell(false)) {
                auto& msg = shell_opt.value();

                json envelope = {
                    {"channel", "shell"},
                    {"topic", msg.header().value("msg_type", "")},
                    {"msg_type", msg.header().value("msg_type", "")},
                    {"parent_msg_id", msg.parent_header().value("msg_id", "")},
                    {"content", msg.content()}
                };
                on_message_callback(envelope.dump());
            }

            // Sleep to prevent 100% CPU utilization on the background thread
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

}
