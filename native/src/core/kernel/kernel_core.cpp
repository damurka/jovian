#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <tuple>

#include "datasuite/json.hpp"
#include "datasuite/history_manager.hpp"
#include "datasuite/request_context.hpp"
#include "kernel_core.hpp"

using namespace std::placeholders;

namespace datasuite
{
    KernelCore::KernelCore(const std::string& kernel_id,
        const std::string& user_name,
        const std::string& session_id,
        logger_ptr logger,
        server_ptr server,
        interpreter_ptr interpreter,
        history_manager_ptr HistoryManager)
        : m_kernelId(std::move(kernel_id))
        , m_userName(std::move(user_name))
        , m_sessionId(std::move(session_id))
        , m_commManager(this)
        , p_logger(logger)
        , p_server(server)
        , p_interpreter(interpreter)
        , p_historyManager(HistoryManager)
    {
        // Request handlers (all but execute_request are blocking)
        m_handler["execute_request"] = handler_type{ &KernelCore::executeRequest, /*blocking*/ false };
        m_handler["complete_request"] = handler_type{ &KernelCore::completeRequest, true };
        m_handler["inspect_request"] = handler_type{ &KernelCore::inspectRequest, true };
        m_handler["history_request"] = handler_type{ &KernelCore::historyRequest, true };
        m_handler["is_complete_request"] = handler_type{ &KernelCore::isCompleteRequest, true };
        m_handler["comm_info_request"] = handler_type{ &KernelCore::commInfoRequest, true };
        m_handler["comm_open"] = handler_type{ &KernelCore::commOpen, true };
        m_handler["comm_close"] = handler_type{ &KernelCore::commClose, true };
        m_handler["comm_msg"] = handler_type{ &KernelCore::commMsg, true };
        m_handler["kernel_info_request"] = handler_type{ &KernelCore::kernelInfoRequest, true };
        m_handler["shutdown_request"] = handler_type{ &KernelCore::shutdownRequest, true };
        m_handler["interrupt_request"] = handler_type{ &KernelCore::interruptRequest, true };

        // Server bindings
        p_server->registerShellListener(std::bind(&KernelCore::dispatchShell, this, _1));
        p_server->registerControlListener(std::bind(&KernelCore::dispatchControl, this, _1));
        p_server->registerStdinListener(std::bind(&KernelCore::dispatchStdin, this, _1));
        p_server->registerInternalListener(std::bind(&KernelCore::dispatchInternal, this, _1));

        // Interpreter bindings
        p_interpreter->registerPublisher([this](RequestContext RequestContext,
            const std::string& msg_type,
            json metadata,
            json content,
            buffer_sequence buffers)
            {
                this->publishMessage(msg_type, RequestContext.header(), std::move(metadata), std::move(content), std::move(buffers),
                    channel::SHELL);
            });

        p_interpreter->registerStdinSender([this](RequestContext RequestContext,
            const std::string& msg_type,
            json metadata,
            json content)
            {
                this->sendStdin(msg_type, RequestContext.id(), RequestContext.header(), std::move(metadata), std::move(content));
            });


        p_interpreter->registerCommManager(&m_commManager);

    }

    KernelCore::~KernelCore()
    {
    }

    PubMessage KernelCore::buildStartMsg() const
    {
        std::string topic = "kernel_core." + m_kernelId + ".status";
        json content;
        content["execution_state"] = "starting";

        PubMessage msg(topic,
            makeHeader("status", m_userName, m_sessionId),
            json::object(),
            json::object(),
            std::move(content),
            buffer_sequence());
        return msg;
    }

    void KernelCore::dispatchShell(Message msg)
    {
        dispatch(std::move(msg), channel::SHELL);
    }

    void KernelCore::dispatchControl(Message msg)
    {
        dispatch(std::move(msg), channel::CONTROL);
    }

    void KernelCore::dispatchStdin(Message msg)
    {
        try
        {
            p_logger->logReceivedMessage(msg, Logger::stdinput);
            const json& content = msg.content();
            std::string value = content.value("value", "");
            p_interpreter->inputReply(value);
        }
        catch (std::exception& e)
        {
            std::cerr << "ERROR: could not handle stdin message" << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }
    }

    json KernelCore::dispatchInternal(json msg)
    {
        json rep = p_interpreter->internalRequest(msg);
        return rep;
    }

    void KernelCore::publishMessage(const std::string& msg_type,
        json parent_header,
        json metadata,
        json content,
        buffer_sequence buffers,
        channel c)
    {
        PubMessage msg(getTopic(msg_type),
            makeHeader(msg_type, m_userName, m_sessionId),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers));
        p_logger->logIopubMessage(msg);
        p_server->publish(std::move(msg), c);
    }

    void KernelCore::sendStdin(const std::string& msg_type,
        const guid_list& id_list,
        json parent_header,
        json metadata,
        json content)
    {
        Message msg(id_list,
            makeHeader(msg_type, m_userName, m_sessionId),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            buffer_sequence());
        p_logger->logSentMessage(msg, Logger::stdinput);
        p_server->sendStdin(std::move(msg));
    }

    CommManager& KernelCore::commManager() & noexcept
    {
        return m_commManager;
    }

    const CommManager& KernelCore::commManager() const& noexcept
    {
        return m_commManager;
    }

    CommManager KernelCore::commManager() const&& noexcept
    {
        return m_commManager;
    }

    const json& KernelCore::parentHeader() const noexcept
    {
        return p_interpreter->parentHeader();
    }

    void KernelCore::dispatch(Message msg, channel c)
    {
        p_logger->logReceivedMessage(msg, c == channel::SHELL ? Logger::shell : Logger::control);
        // Copy because the msg is moved after, and we may need the header
        // for publishing the status.
        json header = msg.header();
        publishStatus(header, "busy", c);

        std::string msg_type = header.value("msg_type", "");
        handler_type handler = getHandler(msg_type);
        if (handler.fptr == nullptr)
        {
            std::cerr << "ERROR: received unknown message" << std::endl;
            std::cerr << "Message type: " << msg_type << std::endl;
        }
        else
        {
            try
            {
                (this->*(handler.fptr))(std::move(msg), c);
            }
            catch (std::exception& e)
            {
                std::cerr << "ERROR: received bad message: " << e.what() << std::endl;
                std::cerr << "Message type: " << msg_type << std::endl;
            }
        }

        // async handlers need to set the idle status themselves
        if (handler.blocking)
        {
            publishStatus(header, "idle", c);
        }
    }

    auto KernelCore::getHandler(const std::string& msg_type) -> handler_type
    {
        auto iter = m_handler.find(msg_type);
        handler_type res = (iter == m_handler.end()) ? handler_type{ nullptr } : iter->second;
        return res;
    }

    void KernelCore::executeRequest(Message request, channel)
    {
        // datasuite assumes execute_request will be executed on SHELL only
        try
        {
            const json& content = request.content();
            std::string code = content.value("code", "");
            bool silent = content.value("silent", false);
            bool store_history = content.value("store_history", true);
            store_history = store_history && !silent;
            json user_expression = content.value("user_expressions", json::object());
            bool allow_stdin = content.value("allow_stdin", true);
            bool stop_on_error = content.value("stop_on_error", false);

            RequestContext RequestContext(request.header(), request.identities());
            ExecuteRequestConfig config{ silent, store_history, allow_stdin };

            auto reply_callback = [this, RequestContext, config, stop_on_error, code](json reply)
                {
                    int execution_count = 1;
                    execution_count = reply.value("execution_count", 1);
                    std::string status;
                    status = reply.value("status", "error");
                    json metadata = getMetadata();

                    sendReply(
                        RequestContext.id(),
                        "execute_reply",
                        RequestContext.header(),
                        std::move(metadata),
                        std::move(reply),
                        channel::SHELL
                    );

                    if (!config.silent && config.store_history)
                    {
                        p_historyManager->storeInputs(0, execution_count, code);
                    }
                    if (!config.silent && status == "error" && stop_on_error)
                    {
                        constexpr long polling_interval = 50;
                        p_server->abortQueue(std::bind(&KernelCore::abortRequest, this, _1), polling_interval);
                    }

                    // idle
                    publishStatus(RequestContext.header(), "idle", channel::SHELL);
                };

            p_interpreter->executeRequest(
                std::move(RequestContext),
                std::move(reply_callback),
                code,
                config,
                std::move(user_expression)
            );
        }
        catch (std::exception& e)
        {
            std::cerr << "ERROR: during execute_request" << std::endl;
            std::cerr << e.what() << std::endl;
        }
    }

    void KernelCore::completeRequest(Message request, channel c)
    {
        const json& content = request.content();
        std::string code = content.value("code", "");
        int cursor_pos = content.value("cursor_pos", -1);
        json reply = p_interpreter->completeRequest(code, cursor_pos);
        sendReply(request.identities(), "complete_reply", request.header(),
            json::object(), std::move(reply), c);
    }

    void KernelCore::inspectRequest(Message request, channel c)
    {
        const json& content = request.content();
        std::string code = content.value("code", "");
        int cursor_pos = content.value("cursor_pos", -1);
        int detail_level = content.value("detail_level", 0);
        json reply = p_interpreter->inspectRequest(code, cursor_pos, detail_level);
        sendReply(request.identities(), "inspect_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::historyRequest(Message request, channel c)
    {
        const json& content = request.content();

        json history = p_historyManager->processRequest(content);

        sendReply(request.identities(), "history_reply", request.header(), json::object(), std::move(history), c);
    }

    void KernelCore::isCompleteRequest(Message request, channel c)
    {
        const json& content = request.content();
        std::string code = content.value("code", "");
        json reply = p_interpreter->isCompleteRequest(code);
        sendReply(request.identities(), "is_complete_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::commInfoRequest(Message request, channel c)
    {
        const json& content = request.content();
        std::string target_name = "";
        if (!content.is_null() && content.contains("target_name") && !content["target_name"].is_null())
        {
            target_name = content["target_name"];
        }
        auto comms = json::object();
        for (auto it = m_commManager.comms().cbegin(); it != m_commManager.comms().cend(); ++it)
        {
            const std::string& name = it->second->target().name();
            if (target_name.empty() || name == target_name)
            {
                json info;
                info["target_name"] = name;
                comms[it->first] = std::move(info);
            }
        }
        json reply;
        reply["comms"] = comms;
        reply["status"] = "ok";
        sendReply(request.identities(), "comm_info_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::kernelInfoRequest(Message request, channel c)
    {
        json reply = p_interpreter->kernelInfoRequest();
        reply["protocol_version"] = getProtocolVersion();
        sendReply(request.identities(), "kernel_info_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::shutdownRequest(Message request, channel c)
    {
        const json& content = request.content();
        bool restart = content.value("restart", false);
        json reply = p_interpreter->shutdownRequest(restart);
        std::string reply_status = reply["status"];
        publishMessage("shutdown", request.header(), json::object(), reply, buffer_sequence(), channel::CONTROL);
        sendReply(request.identities(), "shutdown_reply", request.header(), json::object(), std::move(reply), c);
        if (reply_status == "ok")
        {
            p_server->stop();
        }
    }

    void KernelCore::interruptRequest(Message request, channel c)
    {
        json reply = p_interpreter->interruptRequest();
        std::string reply_status = reply["status"];
        publishMessage("interrupt", request.header(), json::object(), reply, buffer_sequence(), channel::CONTROL);
        sendReply(request.identities(), "interrupt_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::publishStatus(json parent_header, const std::string& status, channel c)
    {
        json content;
        content["execution_state"] = status;
        publishMessage("status", parent_header, json::object(), std::move(content), buffer_sequence(), c);
    }

    void KernelCore::publishExecuteInput(json parent_header,
        const std::string& code,
        int execution_count)
    {
        json content;
        content["code"] = code;
        content["execution_count"] = execution_count;
        publishMessage("execute_input",
            std::move(parent_header),
            json::object(),
            std::move(content),
            buffer_sequence(),
            channel::SHELL);
    }

    void KernelCore::sendReply(const guid_list& id_list,
        const std::string& reply_type,
        json parent_header,
        json metadata,
        json reply_content,
        channel c)
    {
        Message reply(id_list,
            makeHeader(reply_type, m_userName, m_sessionId),
            std::move(parent_header),
            std::move(metadata),
            std::move(reply_content),
            buffer_sequence());
        p_logger->logSentMessage(reply, c == channel::SHELL ? Logger::shell : Logger::control);
        if (c == channel::SHELL)
        {
            p_server->sendShell(std::move(reply));
        }
        else
        {
            p_server->sendControl(std::move(reply));
        }
    }

    void KernelCore::abortRequest(Message msg)
    {
        const json& header = msg.header();
        std::string msg_type = header.value("msg_type", "");
        // replace "_request" part of message type by "_reply"
        msg_type.replace(msg_type.find_last_of('_'), 8, "_reply");
        json content;
        content["status"] = "error";
        sendReply(msg.identities(),
            msg_type,
            json(header),
            json::object(),
            std::move(content),
            channel::SHELL);
    }

    std::string KernelCore::getTopic(const std::string& msg_type) const
    {
        return "kernel_core." + m_kernelId + "." + msg_type;
    }

    json KernelCore::getMetadata() const
    {
        json metadata;
        metadata["started"] = iso8601Now();
        return metadata;
    }

    void KernelCore::commOpen(Message request, channel)
    {
        m_commManager.commOpen(std::move(request));
    }

    void KernelCore::commClose(Message request, channel)
    {
        m_commManager.commClose(std::move(request));
    }

    void KernelCore::commMsg(Message request, channel)
    {
        m_commManager.commMsg(std::move(request));
    }
}
