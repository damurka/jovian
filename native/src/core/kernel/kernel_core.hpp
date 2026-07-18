#ifndef DATASUITE_KERNEL_CORE_HPP
#define DATASUITE_KERNEL_CORE_HPP

#include <map>
#include <string>

#include "nlohmann/json.hpp"

#include "datasuite/comm.hpp"
#include "datasuite/server.hpp"
#include "datasuite/interpreter.hpp"
#include "datasuite/history_manager.hpp"
#include "datasuite/message.hpp"
#include "datasuite/logger.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    class kernel_core
    {
    public:

        using logger_ptr = logger*;
        using server_ptr = server*;
        using interpreter_ptr = interpreter*;
        using history_manager_ptr = history_manager*;
        using guid_list = message::guid_list;

        kernel_core(const std::string& kernel_id,
            const std::string& user_name,
            const std::string& session_id,
            logger_ptr logger,
            server_ptr server,
            interpreter_ptr p_interpreter,
            history_manager_ptr p_history_manager);

        ~kernel_core();

        pub_message build_start_msg() const;

        void dispatch_shell(message msg);
        void dispatch_control(message msg);
        void dispatch_stdin(message msg);
        nl::json dispatch_internal(nl::json msg);

        void publish_message(const std::string& msg_type,
            nl::json parent_header,
            nl::json metadata,
            nl::json content,
            buffer_sequence buffers,
            channel origin);

        void send_stdin(const std::string& msg_type, const guid_list& id_list, nl::json parent_header, nl::json metadata, nl::json content);

        comm_manager& comm_manager() & noexcept;
        const datasuite::comm_manager& comm_manager() const& noexcept;
        datasuite::comm_manager comm_manager() const&& noexcept;

        const nl::json& parent_header() const noexcept;

    private:

        using handler_fptr_type = void (kernel_core::*)(message, channel);

        struct handler_type {
            handler_fptr_type fptr = nullptr;
            bool blocking = true;
        };


        void dispatch(message msg, channel c);

        handler_type get_handler(const std::string& msg_type);

        void execute_request(message request, channel c);
        void complete_request(message request, channel c);
        void inspect_request(message request, channel c);
        void history_request(message request, channel c);
        void is_complete_request(message request, channel c);
        void comm_info_request(message request, channel c);
        void comm_open(message request, channel c);
        void comm_close(message request, channel c);
        void comm_msg(message request, channel c);

        void kernel_info_request(message request, channel c);
        void shutdown_request(message request, channel c);
        void interrupt_request(message request, channel c);
        void debug_request(message request, channel c);

        void publish_status(nl::json parent_header, const std::string& status, channel c);
        void publish_execute_input(nl::json parent_header, const std::string& code, int execution_count);

        void send_reply(const guid_list& id_list,
            const std::string& reply_type,
            nl::json parent_header,
            nl::json metadata,
            nl::json reply_content,
            channel c);

        void abort_request(message msg);

        std::string get_topic(const std::string& msg_type) const;
        nl::json get_metadata() const;


        std::string m_kernel_id;
        std::string m_user_name;
        std::string m_session_id;

        std::map<std::string, handler_type> m_handler;
        datasuite::comm_manager m_comm_manager;
        logger_ptr p_logger;
        server_ptr p_server;
        interpreter_ptr p_interpreter;
        history_manager_ptr p_history_manager;
    };
}

#endif
