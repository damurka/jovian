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
#include "transport/client/client_zmq.hpp"

namespace datasuite
{

    // =========================================================================
    // ENVIRONMENT CONFIGURATION
    // =========================================================================
    struct DATASUITE_API EnvironmentConfig {
        std::string r_home;
        std::string r_path;
        std::string r_libs;
    };

    // =========================================================================
    // 1. THE SERVER CLASS
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
        void setup_environment();
    };

    // =========================================================================
    // 2. THE CLIENT CLASS
    // =========================================================================
    class DATASUITE_API DatasuiteClient {
    private:
        std::unique_ptr<context> client_context;
        std::unique_ptr<ClientZmq> zmq_client;

    public:
        DatasuiteClient() = default;
        ~DatasuiteClient() { stop(); }

        void start(const KernelConfiguration& config);
        std::string execute(const std::string& code);
        void stop();

        // Expose the underlying client for the Engine's polling thread
        ClientZmq* get_zmq_client() { return zmq_client.get(); }
    };

    // =========================================================================
    // 3. THE COMBINED ENGINE (Facade for Node.js)
    // =========================================================================
    class DATASUITE_API DatasuiteEngine 
    {
    public:
        DatasuiteEngine();
        explicit DatasuiteEngine(const EnvironmentConfig& env);
        ~DatasuiteEngine() { stop(); }

		void init();
        void start(std::function<void(std::string)> callback);
        std::string execute(const std::string& code);
        void stop();

    private:
        KernelConfiguration config;
        EnvironmentConfig env_config;
        DatasuiteServer server;
        DatasuiteClient client;

        std::thread polling_thread;
        std::atomic<bool> is_running{ false };
		std::atomic<bool> is_initialized{ false };

        // Callback used to send a JSON envelope back to TypeScript:
        // {channel, topic, msg_type, parent_msg_id, content}
        std::function<void(std::string)> on_message_callback;

        void poll_messages();
    };
}

#endif // DATASUITE_ENGINE_HPP