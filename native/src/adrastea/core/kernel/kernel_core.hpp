#ifndef ADRASTEA_KERNEL_CORE_HPP
#define ADRASTEA_KERNEL_CORE_HPP

#include <map>
#include <string>

#include "adrastea/json.hpp"
#include "adrastea/comm.hpp"
#include "adrastea/server.hpp"
#include "adrastea/interpreter.hpp"
#include "adrastea/history_manager.hpp"
#include "adrastea/message.hpp"
#include "adrastea/logger.hpp"

namespace adrastea
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

        PubMessage buildStartMsg() const;

        void dispatchShell(Message msg);
        void dispatchControl(Message msg);
        void dispatchStdin(Message msg);
        json dispatchInternal(json msg);

        void publishMessage(const std::string& msg_type,
            json parent_header,
            json metadata,
            json content,
            buffer_sequence buffers,
            channel origin);

        void sendStdin(const std::string& msg_type, const guid_list& id_list, json parent_header, json metadata, json content);

        CommManager& commManager() & noexcept;
        const adrastea::CommManager& commManager() const& noexcept;
        adrastea::CommManager commManager() const&& noexcept;

        const json& parentHeader() const noexcept;

    private:

        using handler_fptr_type = void (KernelCore::*)(Message, channel);

        struct handler_type {
            handler_fptr_type fptr = nullptr;
            bool blocking = true;
        };


        void dispatch(Message msg, channel c);

        handler_type getHandler(const std::string& msg_type);

        void executeRequest(Message request, channel c);
        void completeRequest(Message request, channel c);
        void inspectRequest(Message request, channel c);
        void historyRequest(Message request, channel c);
        void isCompleteRequest(Message request, channel c);
        void commInfoRequest(Message request, channel c);
        void commOpen(Message request, channel c);
        void commClose(Message request, channel c);
        void commMsg(Message request, channel c);

        void kernelInfoRequest(Message request, channel c);
        void shutdownRequest(Message request, channel c);
        void interruptRequest(Message request, channel c);
        void debugRequest(Message request, channel c);

        void publishStatus(json parent_header, const std::string& status, channel c);
        void publishExecuteInput(json parent_header, const std::string& code, int execution_count);

        void sendReply(const guid_list& id_list,
            const std::string& reply_type,
            json parent_header,
            json metadata,
            json reply_content,
            channel c);

        void abortRequest(Message msg);

        std::string getTopic(const std::string& msg_type) const;
        json getMetadata() const;


        std::string m_kernelId;
        std::string m_userName;
        std::string m_sessionId;

        std::map<std::string, handler_type> m_handler;
        adrastea::CommManager m_commManager;
        logger_ptr p_logger;
        server_ptr p_server;
        interpreter_ptr p_interpreter;
        history_manager_ptr p_historyManager;
    };
}

#endif
