#ifndef DATASUITE_KERNEL_HPP
#define DATASUITE_KERNEL_HPP

#include <memory>
#include <string>
#include <functional>

#include "datasuite.hpp"
#include "context.hpp"
#include "history_manager.hpp"
#include "interpreter.hpp"
#include "kernel_configuration.hpp"
#include "server.hpp"
#include "logger.hpp"

namespace datasuite
{
    class KernelCore;

    DATASUITE_API
    std::string get_user_name();

    class DATASUITE_API kernel
    {
    public:

        using context_ptr = std::unique_ptr<context>;
        using history_manager_ptr = std::unique_ptr<HistoryManager>;
        using interpreter_ptr = std::unique_ptr<interpreter>;
        using kernel_core_ptr = std::unique_ptr<KernelCore>;
        using logger_ptr = std::unique_ptr<logger>;
        using server_ptr = std::unique_ptr<server>;
        using server_builder = std::function<server_ptr(context& context,
            const configuration& config,
            nl::json::error_handler_t eh)>;

        kernel(configuration config,
            const std::string& user_name,
            context_ptr context,
            interpreter_ptr interpreter,
            server_builder sbuilder,
            history_manager_ptr HistoryManager = make_in_memory_history_manager(),
            logger_ptr logger = nullptr,
            nl::json::error_handler_t eh = nl::json::error_handler_t::strict);

        kernel(const std::string& user_name,
            context_ptr context,
            interpreter_ptr interpreter,
            server_builder sbuilder,
            history_manager_ptr HistoryManager = make_in_memory_history_manager(),
            logger_ptr logger = nullptr,
            nl::json::error_handler_t eh = nl::json::error_handler_t::strict);

        ~kernel();

        void start();
        void stop();

        const KernelConfiguration& get_config();
        server& get_server();

    private:

        KernelConfiguration m_config;
        std::string m_kernelId;
        std::string m_sessionId;
        std::string m_userName;
        // The context must be declared before any other
        // middleware component since it must be destroyed
        // last
        context_ptr p_context;
        interpreter_ptr p_interpreter;
        history_manager_ptr p_historyManager;
        logger_ptr p_logger;
        server_ptr p_server;
        kernel_core_ptr p_core;
        nl::json::error_handler_t m_errorHandler;
    };
}

#endif