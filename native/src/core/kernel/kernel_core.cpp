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
        m_handler["execute_request"] = handler_type{ &KernelCore::execute_request, /*blocking*/ false };
        m_handler["complete_request"] = handler_type{ &KernelCore::complete_request, true };
        m_handler["inspect_request"] = handler_type{ &KernelCore::inspect_request, true };
        m_handler["history_request"] = handler_type{ &KernelCore::history_request, true };
        m_handler["is_complete_request"] = handler_type{ &KernelCore::is_complete_request, true };
        m_handler["comm_info_request"] = handler_type{ &KernelCore::comm_info_request, true };
        m_handler["comm_open"] = handler_type{ &KernelCore::comm_open, true };
        m_handler["comm_close"] = handler_type{ &KernelCore::comm_close, true };
        m_handler["comm_msg"] = handler_type{ &KernelCore::comm_msg, true };
        m_handler["kernel_info_request"] = handler_type{ &KernelCore::kernel_info_request, true };
        m_handler["shutdown_request"] = handler_type{ &KernelCore::shutdown_request, true };
        m_handler["interrupt_request"] = handler_type{ &KernelCore::interrupt_request, true };

        // Server bindings
        p_server->register_shell_listener(std::bind(&KernelCore::dispatch_shell, this, _1));
        p_server->register_control_listener(std::bind(&KernelCore::dispatch_control, this, _1));
        p_server->register_stdin_listener(std::bind(&KernelCore::dispatch_stdin, this, _1));
        p_server->register_internal_listener(std::bind(&KernelCore::dispatch_internal, this, _1));

        // Interpreter bindings
        p_interpreter->register_publisher([this](RequestContext RequestContext,
            const std::string& msg_type,
            json metadata,
            json content,
            buffer_sequence buffers)
            {
                this->publish_message(msg_type, RequestContext.header(), std::move(metadata), std::move(content), std::move(buffers),
                    channel::SHELL);
            });

        p_interpreter->register_stdin_sender([this](RequestContext RequestContext,
            const std::string& msg_type,
            json metadata,
            json content)
            {
                this->send_stdin(msg_type, RequestContext.id(), RequestContext.header(), std::move(metadata), std::move(content));
            });


        p_interpreter->register_comm_manager(&m_commManager);

    }

    KernelCore::~KernelCore()
    {
    }

    PubMessage KernelCore::build_start_msg() const
    {
        std::string topic = "kernel_core." + m_kernelId + ".status";
        json content;
        content["execution_state"] = "starting";

        PubMessage msg(topic,
            make_header("status", m_userName, m_sessionId),
            json::object(),
            json::object(),
            std::move(content),
            buffer_sequence());
        return msg;
    }

    void KernelCore::dispatch_shell(Message msg)
    {
        dispatch(std::move(msg), channel::SHELL);
    }

    void KernelCore::dispatch_control(Message msg)
    {
        dispatch(std::move(msg), channel::CONTROL);
    }

    void KernelCore::dispatch_stdin(Message msg)
    {
        try
        {
            p_logger->log_received_message(msg, Logger::stdinput);
            const json& content = msg.content();
            std::string value = content.value("value", "");
            p_interpreter->input_reply(value);
        }
        catch (std::exception& e)
        {
            std::cerr << "ERROR: could not handle stdin message" << std::endl;
            std::cerr << e.what() << std::endl;
            return;
        }
    }

    json KernelCore::dispatch_internal(json msg)
    {
        json rep = p_interpreter->internal_request(msg);
        return rep;
    }

    void KernelCore::publish_message(const std::string& msg_type,
        json parent_header,
        json metadata,
        json content,
        buffer_sequence buffers,
        channel c)
    {
        PubMessage msg(get_topic(msg_type),
            make_header(msg_type, m_userName, m_sessionId),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers));
        p_logger->log_iopub_message(msg);
        p_server->publish(std::move(msg), c);
    }

    void KernelCore::send_stdin(const std::string& msg_type,
        const guid_list& id_list,
        json parent_header,
        json metadata,
        json content)
    {
        Message msg(id_list,
            make_header(msg_type, m_userName, m_sessionId),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            buffer_sequence());
        p_logger->log_sent_message(msg, Logger::stdinput);
        p_server->send_stdin(std::move(msg));
    }

    CommManager& KernelCore::comm_manager() & noexcept
    {
        return m_commManager;
    }

    const CommManager& KernelCore::comm_manager() const& noexcept
    {
        return m_commManager;
    }

    CommManager KernelCore::comm_manager() const&& noexcept
    {
        return m_commManager;
    }

    const json& KernelCore::parent_header() const noexcept
    {
        return p_interpreter->parent_header();
    }

    void KernelCore::dispatch(Message msg, channel c)
    {
        p_logger->log_received_message(msg, c == channel::SHELL ? Logger::shell : Logger::control);
        // Copy because the msg is moved after, and we may need the header
        // for publishing the status.
        json header = msg.header();
        publish_status(header, "busy", c);

        std::string msg_type = header.value("msg_type", "");
        handler_type handler = get_handler(msg_type);
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
            publish_status(header, "idle", c);
        }
    }

    auto KernelCore::get_handler(const std::string& msg_type) -> handler_type
    {
        auto iter = m_handler.find(msg_type);
        handler_type res = (iter == m_handler.end()) ? handler_type{ nullptr } : iter->second;
        return res;
    }

    void KernelCore::execute_request(Message request, channel)
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
                    json metadata = get_metadata();

                    send_reply(
                        RequestContext.id(),
                        "execute_reply",
                        RequestContext.header(),
                        std::move(metadata),
                        std::move(reply),
                        channel::SHELL
                    );

                    if (!config.silent && config.store_history)
                    {
                        p_historyManager->store_inputs(0, execution_count, code);
                    }
                    if (!config.silent && status == "error" && stop_on_error)
                    {
                        constexpr long polling_interval = 50;
                        p_server->abort_queue(std::bind(&KernelCore::abort_request, this, _1), polling_interval);
                    }

                    // idle
                    publish_status(RequestContext.header(), "idle", channel::SHELL);
                };

            p_interpreter->execute_request(
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

    void KernelCore::complete_request(Message request, channel c)
    {
        const json& content = request.content();
        std::string code = content.value("code", "");
        int cursor_pos = content.value("cursor_pos", -1);
        json reply = p_interpreter->complete_request(code, cursor_pos);
        send_reply(request.identities(), "complete_reply", request.header(),
            json::object(), std::move(reply), c);
    }

    void KernelCore::inspect_request(Message request, channel c)
    {
        const json& content = request.content();
        std::string code = content.value("code", "");
        int cursor_pos = content.value("cursor_pos", -1);
        int detail_level = content.value("detail_level", 0);
        json reply = p_interpreter->inspect_request(code, cursor_pos, detail_level);
        send_reply(request.identities(), "inspect_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::history_request(Message request, channel c)
    {
        const json& content = request.content();

        json history = p_historyManager->process_request(content);

        send_reply(request.identities(), "history_reply", request.header(), json::object(), std::move(history), c);
    }

    void KernelCore::is_complete_request(Message request, channel c)
    {
        const json& content = request.content();
        std::string code = content.value("code", "");
        json reply = p_interpreter->is_complete_request(code);
        send_reply(request.identities(), "is_complete_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::comm_info_request(Message request, channel c)
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
        send_reply(request.identities(), "comm_info_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::kernel_info_request(Message request, channel c)
    {
        json reply = p_interpreter->kernel_info_request();
        reply["protocol_version"] = get_protocol_version();
        send_reply(request.identities(), "kernel_info_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::shutdown_request(Message request, channel c)
    {
        const json& content = request.content();
        bool restart = content.value("restart", false);
        json reply = p_interpreter->shutdown_request(restart);
        std::string reply_status = reply["status"];
        publish_message("shutdown", request.header(), json::object(), reply, buffer_sequence(), channel::CONTROL);
        send_reply(request.identities(), "shutdown_reply", request.header(), json::object(), std::move(reply), c);
        if (reply_status == "ok")
        {
            p_server->stop();
        }
    }

    void KernelCore::interrupt_request(Message request, channel c)
    {
        json reply = p_interpreter->interrupt_request();
        std::string reply_status = reply["status"];
        publish_message("interrupt", request.header(), json::object(), reply, buffer_sequence(), channel::CONTROL);
        send_reply(request.identities(), "interrupt_reply", request.header(), json::object(), std::move(reply), c);
    }

    void KernelCore::publish_status(json parent_header, const std::string& status, channel c)
    {
        json content;
        content["execution_state"] = status;
        publish_message("status", parent_header, json::object(), std::move(content), buffer_sequence(), c);
    }

    void KernelCore::publish_execute_input(json parent_header,
        const std::string& code,
        int execution_count)
    {
        json content;
        content["code"] = code;
        content["execution_count"] = execution_count;
        publish_message("execute_input",
            std::move(parent_header),
            json::object(),
            std::move(content),
            buffer_sequence(),
            channel::SHELL);
    }

    void KernelCore::send_reply(const guid_list& id_list,
        const std::string& reply_type,
        json parent_header,
        json metadata,
        json reply_content,
        channel c)
    {
        Message reply(id_list,
            make_header(reply_type, m_userName, m_sessionId),
            std::move(parent_header),
            std::move(metadata),
            std::move(reply_content),
            buffer_sequence());
        p_logger->log_sent_message(reply, c == channel::SHELL ? Logger::shell : Logger::control);
        if (c == channel::SHELL)
        {
            p_server->send_shell(std::move(reply));
        }
        else
        {
            p_server->send_control(std::move(reply));
        }
    }

    void KernelCore::abort_request(Message msg)
    {
        const json& header = msg.header();
        std::string msg_type = header.value("msg_type", "");
        // replace "_request" part of message type by "_reply"
        msg_type.replace(msg_type.find_last_of('_'), 8, "_reply");
        json content;
        content["status"] = "error";
        send_reply(msg.identities(),
            msg_type,
            json(header),
            json::object(),
            std::move(content),
            channel::SHELL);
    }

    std::string KernelCore::get_topic(const std::string& msg_type) const
    {
        return "kernel_core." + m_kernelId + "." + msg_type;
    }

    json KernelCore::get_metadata() const
    {
        json metadata;
        metadata["started"] = iso8601_now();
        return metadata;
    }

    void KernelCore::comm_open(Message request, channel)
    {
        m_commManager.comm_open(std::move(request));
    }

    void KernelCore::comm_close(Message request, channel)
    {
        m_commManager.comm_close(std::move(request));
    }

    void KernelCore::comm_msg(Message request, channel)
    {
        m_commManager.comm_msg(std::move(request));
    }
}
