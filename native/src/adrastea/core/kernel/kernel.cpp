#include <iostream>
#include <string>
#include <random>

#include "adrastea/json.hpp"
#include "adrastea/kernel.hpp"
#include "adrastea/guid.hpp"
#include "adrastea/history_manager.hpp"
#include "adrastea/control_messenger.hpp"
#include "kernel_core.hpp"
#include "adrastea/utils/logger_impl.hpp"

#if (defined(__linux__) || defined(__unix__))
#define LINUX_PLATFORM
#elif (defined(_WIN32) || defined(_WIN64))
#define WINDOWS_PLATFORM
#elif defined(__APPLE__)
#define APPLE_PLATFORM
#endif

#if (defined(LINUX_PLATFORM) || defined(APPLE_PLATFORM))
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#elif (defined(_WIN32) || defined(_WIN64))
#include <windows.h>
#include <Lmcons.h>
#endif

namespace adrastea
{
    std::string getUserName() {
#if (defined(LINUX_PLATFORM) || defined(APPLE_PLATFORM))
        struct passwd* pws;
        pws = getpwuid(geteuid());
        if (pws != nullptr)
        {
            std::string res = pws->pw_name;
            return res;
        }
        else
        {
            const char* user = std::getenv("USER");
            if (user != nullptr)
            {
                std::string res = user;
                return res;
            }
            else
            {
                return "unspecified user";
            }
        }
#elif defined(WINDOWS_PLATFORM)
        char username[UNLEN + 1];
        DWORD username_len = UNLEN + 1;
        GetUserNameA(username, &username_len);
        return username;
#else
        return "unspecified user";
#endif
    }

    Kernel::Kernel(configuration config,
        const std::string& user_name,
        context_ptr context,
        interpreter_ptr interpreter,
        server_builder sbuilder,
        history_manager_ptr HistoryManager,
        logger_ptr logger,
        json::error_handler_t eh)
        : m_kernelId(newGuid())
        , m_sessionId(newGuid())
        , m_userName(user_name)
        , p_context(std::move(context))
        , p_interpreter(std::move(interpreter))
        , p_historyManager(std::move(HistoryManager))
        , p_logger(std::move(logger))
        , m_errorHandler(eh)
    {
        std::visit([this](auto& arg)
            {
                if (arg.m_key.size() == 0)
                {
                    arg.m_key = newGuid();
                }
                m_config.m_transport = arg.m_transport;
                m_config.m_ip = arg.m_ip;
                m_config.m_signatureScheme = arg.m_signatureScheme;
                m_config.m_key = arg.m_key;
            }, config);

        if (p_logger == nullptr || std::getenv("ADRASTEA_LOG") == nullptr)
        {
            p_logger = std::make_unique<LoggerNolog>();
        }

        p_server = sbuilder(*p_context, config, m_errorHandler);
        p_server->updateConfig(m_config);

        p_core = std::make_unique<KernelCore>(m_kernelId,
            m_userName,
            m_sessionId,
            p_logger.get(),
            p_server.get(),
            p_interpreter.get(),
            p_historyManager.get());

        ControlMessenger& messenger = p_server->getControlMessenger();

        p_interpreter->registerControlMessenger(messenger);
        p_interpreter->registerHistoryManager(*p_historyManager);
        p_interpreter->configure();
    }

    Kernel::Kernel(const std::string& user_name,
        context_ptr context,
        interpreter_ptr interpreter,
        server_builder sbuilder,
        history_manager_ptr HistoryManager,
        logger_ptr logger,
        json::error_handler_t eh)
        : Kernel(
            KernelConfiguration{},
            user_name,
            std::move(context),
            std::move(interpreter),
            std::move(sbuilder),
            std::move(HistoryManager),
            std::move(logger),
            eh)
    {
    }

    Kernel::~Kernel()
    {
    }

    void Kernel::start()
    {
        PubMessage start_msg = p_core->buildStartMsg();
        p_server->start(std::move(start_msg));
    }

    void Kernel::stop()
    {
        p_interpreter->shutdownRequest(false);
        p_server->stop();
    }

    const KernelConfiguration& Kernel::getConfig()
    {
        return m_config;
    }

    Server& Kernel::getServer()
    {
        return *p_server;
    }
}
