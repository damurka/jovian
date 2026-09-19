#ifndef ADRASTEA_KERNEL_HPP
#define ADRASTEA_KERNEL_HPP

#include <memory>
#include <string>
#include <functional>

#include "adrastea.hpp"
#include "context.hpp"
#include "history_manager.hpp"
#include "interpreter.hpp"
#include "kernel_configuration.hpp"
#include "server.hpp"
#include "logger.hpp"

namespace adrastea
{
    class KernelCore;

    ADRASTEA_API
    std::string getUserName();

    class ADRASTEA_API Kernel
    {
    public:

        using context_ptr = std::unique_ptr<Context>;
        using history_manager_ptr = std::unique_ptr<HistoryManager>;
        using interpreter_ptr = std::unique_ptr<Interpreter>;
        using kernel_core_ptr = std::unique_ptr<KernelCore>;
        using logger_ptr = std::unique_ptr<Logger>;
        using server_ptr = std::unique_ptr<Server>;
        using server_builder = std::function<server_ptr(Context& context,
            const configuration& config,
            json::error_handler_t eh)>;

        Kernel(configuration config,
            const std::string& user_name,
            context_ptr context,
            interpreter_ptr interpreter,
            server_builder sbuilder,
            history_manager_ptr HistoryManager = makeInMemoryHistoryManager(),
            logger_ptr logger = nullptr,
            json::error_handler_t eh = json::error_handler_t::strict);

        Kernel(const std::string& user_name,
            context_ptr context,
            interpreter_ptr interpreter,
            server_builder sbuilder,
            history_manager_ptr HistoryManager = makeInMemoryHistoryManager(),
            logger_ptr logger = nullptr,
            json::error_handler_t eh = json::error_handler_t::strict);

        ~Kernel();

        void start();
        void stop();

        const KernelConfiguration& getConfig();
        Server& getServer();

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
        json::error_handler_t m_errorHandler;
    };
}

#endif
