#include <iostream>
#include <string>
#include <random>

#include "datasuite/json.hpp"
#include "datasuite/kernel.hpp"
#include "datasuite/guid.hpp"
#include "datasuite/history_manager.hpp"
#include "datasuite/control_messenger.hpp"
#include "kernel_core.hpp"
#include "utils/logger_impl.hpp"

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

namespace datasuite
{
    std::string get_user_name() {
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

    kernel::kernel(configuration config,
        const std::string& user_name,
        context_ptr context,
        interpreter_ptr interpreter,
        server_builder sbuilder,
        history_manager_ptr HistoryManager,
        logger_ptr logger,
        json::error_handler_t eh)
        : m_kernelId(new_guid())
        , m_sessionId(new_guid())
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
                    arg.m_key = new_guid();
                }
                m_config.m_transport = arg.m_transport;
                m_config.m_ip = arg.m_ip;
                m_config.m_signatureScheme = arg.m_signatureScheme;
                m_config.m_key = arg.m_key;
            }, config);

        if (p_logger == nullptr || std::getenv("DATASUITE_LOG") == nullptr)
        {
            p_logger = std::make_unique<logger_nolog>();
        }

        p_server = sbuilder(*p_context, config, m_errorHandler);
        p_server->update_config(m_config);

        p_core = std::make_unique<KernelCore>(m_kernelId,
            m_userName,
            m_sessionId,
            p_logger.get(),
            p_server.get(),
            p_interpreter.get(),
            p_historyManager.get());

        control_messenger& messenger = p_server->get_control_messenger();

        p_interpreter->register_control_messenger(messenger);
        p_interpreter->register_history_manager(*p_historyManager);
        p_interpreter->configure();
    }

    kernel::kernel(const std::string& user_name,
        context_ptr context,
        interpreter_ptr interpreter,
        server_builder sbuilder,
        history_manager_ptr HistoryManager,
        logger_ptr logger,
        json::error_handler_t eh)
        : kernel(
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

    kernel::~kernel()
    {
    }

    void kernel::start()
    {
        pub_message start_msg = p_core->build_start_msg();
        p_server->start(std::move(start_msg));
    }

    void kernel::stop()
    {
        p_interpreter->shutdown_request(false);
        p_server->stop();
    }

    const KernelConfiguration& kernel::get_config()
    {
        return m_config;
    }

    server& kernel::get_server()
    {
        return *p_server;
    }
}
