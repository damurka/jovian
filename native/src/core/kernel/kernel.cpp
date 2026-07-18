#include <iostream>
#include <string>
#include <random>

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
#if defined(DATASUITE_EMSCRIPTEN_WASM_BUILD)
        return "unspecified user";
#elif (defined(LINUX_PLATFORM) || defined(APPLE_PLATFORM))
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
        history_manager_ptr history_manager,
        logger_ptr logger,
        nl::json::error_handler_t eh)
        : m_kernel_id(new_guid())
        , m_session_id(new_guid())
        , m_user_name(user_name)
        , p_context(std::move(context))
        , p_interpreter(std::move(interpreter))
        , p_history_manager(std::move(history_manager))
        , p_logger(std::move(logger))
        , m_error_handler(eh)
    {
        std::visit([this](auto& arg)
            {
                if (arg.m_key.size() == 0)
                {
                    arg.m_key = new_guid();
                }
                m_config.m_transport = arg.m_transport;
                m_config.m_ip = arg.m_ip;
                m_config.m_signature_scheme = arg.m_signature_scheme;
                m_config.m_key = arg.m_key;
            }, config);

        if (p_logger == nullptr || std::getenv("DATASUITE_LOG") == nullptr)
        {
            p_logger = std::make_unique<logger_nolog>();
        }

        p_server = sbuilder(*p_context, config, m_error_handler);
        p_server->update_config(m_config);

        p_core = std::make_unique<kernel_core>(m_kernel_id,
            m_user_name,
            m_session_id,
            p_logger.get(),
            p_server.get(),
            p_interpreter.get(),
            p_history_manager.get());

        control_messenger& messenger = p_server->get_control_messenger();

        p_interpreter->register_control_messenger(messenger);
        p_interpreter->register_history_manager(*p_history_manager);
        p_interpreter->configure();
    }

    kernel::kernel(const std::string& user_name,
        context_ptr context,
        interpreter_ptr interpreter,
        server_builder sbuilder,
        history_manager_ptr history_manager,
        logger_ptr logger,
        nl::json::error_handler_t eh)
        : kernel(
            kernel_configuration{},
            user_name,
            std::move(context),
            std::move(interpreter),
            std::move(sbuilder),
            std::move(history_manager),
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

    const kernel_configuration& kernel::get_config()
    {
        return m_config;
    }

    server& kernel::get_server()
    {
        return *p_server;
    }
}
