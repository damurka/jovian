#ifndef DATASUITE_KERNEL_CORE_HPP
#define DATASUITE_KERNEL_CORE_HPP

#include <map>
#include <string>

#include "datasuite/json.hpp"
#include "datasuite/comm.hpp"
#include "datasuite/server.hpp"
#include "datasuite/interpreter.hpp"
#include "datasuite/history_manager.hpp"
#include "datasuite/message.hpp"
#include "datasuite/logger.hpp"

namespace datasuite
{
    class KernelCore
    {
    public:

        using logger_ptr = Logger*;
        using server_ptr = Server*;
        using interpreter_ptr = Interpreter*;
        using history_manager_ptr = HistoryManager*;
        using guid_list = Message::guid_list;

        KernelCore(const std::string& kernel_id,
            const std::string& user_name,
            const std::string& session_id,
            logger_ptr logger,
            server_ptr server,
            interpreter_ptr p_interpreter,
            history_manager_ptr p_historyManager);

        ~KernelCore();

        PubMessage build_start_msg() const;

        void dispatch_shell(Message msg);
        void dispatch_control(Message msg);
        void dispatch_stdin(Message msg);
        json dispatch_internal(json msg);

        void publish_message(const std::string& msg_type,
            json parent_header,
            json metadata,
            json content,
            buffer_sequence buffers,
            channel origin);

        void send_stdin(const std::string& msg_type, const guid_list& id_list, json parent_header, json metadata, json content);

        CommManager& comm_manager() & noexcept;
        const datasuite::CommManager& comm_manager() const& noexcept;
        datasuite::CommManager comm_manager() const&& noexcept;

        const json& parent_header() const noexcept;

    private:

        using handler_fptr_type = void (KernelCore::*)(Message, channel);

        struct handler_type {
            handler_fptr_type fptr = nullptr;
            bool blocking = true;
        };


        void dispatch(Message msg, channel c);

        handler_type get_handler(const std::string& msg_type);

        void execute_request(Message request, channel c);
        void complete_request(Message request, channel c);
        void inspect_request(Message request, channel c);
        void history_request(Message request, channel c);
        void is_complete_request(Message request, channel c);
        void comm_info_request(Message request, channel c);
        void comm_open(Message request, channel c);
        void comm_close(Message request, channel c);
        void comm_msg(Message request, channel c);

        void kernel_info_request(Message request, channel c);
        void shutdown_request(Message request, channel c);
        void interrupt_request(Message request, channel c);
        void debug_request(Message request, channel c);

        void publish_status(json parent_header, const std::string& status, channel c);
        void publish_execute_input(json parent_header, const std::string& code, int execution_count);

        void send_reply(const guid_list& id_list,
            const std::string& reply_type,
            json parent_header,
            json metadata,
            json reply_content,
            channel c);

        void abort_request(Message msg);

        std::string get_topic(const std::string& msg_type) const;
        json get_metadata() const;


        std::string m_kernelId;
        std::string m_userName;
        std::string m_sessionId;

        std::map<std::string, handler_type> m_handler;
        datasuite::CommManager m_commManager;
        logger_ptr p_logger;
        server_ptr p_server;
        interpreter_ptr p_interpreter;
        history_manager_ptr p_historyManager;
    };
}

#endif
