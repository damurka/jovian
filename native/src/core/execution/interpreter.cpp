#include <string>
#include <utility>
#include <vector>

#include "datasuite/json.hpp"
#include "datasuite/interpreter.hpp"

namespace datasuite
{
    Interpreter::Interpreter()
        : m_executionCount(0)
    {
    }

    void Interpreter::configure()
    {
        configure_impl();
    }

    void Interpreter::execute_request(RequestContext context,
        send_reply_callback callback,
        const std::string& code,
        ExecuteRequestConfig config,
        json user_expressions)
    {
        set_request_context(std::move(context));
        if (!config.silent)
        {
            ++m_executionCount;
            publish_execution_input(code, m_executionCount);
        }
        // copy m_executionCount in a local variable to capture it in the lambda
        auto execution_count = m_executionCount;

        auto callback_impl = [execution_count, callback = std::move(callback)](json reply)
            {
                reply["execution_count"] = execution_count;
                callback(std::move(reply));
            };

        execute_request_impl(
            std::move(callback_impl),
            m_executionCount,
            code,
            std::move(config),
            user_expressions
        );
    }

    json Interpreter::complete_request(const std::string& code, int cursor_pos)
    {
        return complete_request_impl(code, cursor_pos);
    }

    json Interpreter::inspect_request(const std::string& code, int cursor_pos, int detail_level)
    {
        return inspect_request_impl(code, cursor_pos, detail_level);
    }

    json Interpreter::is_complete_request(const std::string& code)
    {
        return is_complete_request_impl(code);
    }

    json Interpreter::kernel_info_request()
    {
        return kernel_info_request_impl();
    }

    json Interpreter::shutdown_request(bool restart)
    {
        return shutdown_request_impl(restart);
    }

    json Interpreter::interrupt_request()
    {
        return interrupt_request_impl();
    }

    json Interpreter::internal_request(const json& message)
    {
        return internal_request_impl(message);
    }

    void Interpreter::register_publisher(const publisher_type& publisher)
    {
        m_publisher = publisher;
    }

    void Interpreter::publish_stream(const std::string& name, const std::string& text)
    {
        if (m_publisher)
        {
            json content;
            content["name"] = name;
            content["text"] = text;
            m_publisher(
                get_request_context(),
                "stream",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::display_data(json data, json metadata, json transient)
    {
        if (m_publisher)
        {
            m_publisher(
                get_request_context(),
                "display_data",
                json::object(),
                build_display_content(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void Interpreter::update_display_data(json data, json metadata, json transient)
    {
        if (m_publisher)
        {
            m_publisher(
                get_request_context(),
                "update_display_data",
                json::object(),
                build_display_content(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publish_execution_input(const std::string& code, int execution_count)
    {
        if (m_publisher)
        {
            json content;
            content["code"] = code;
            content["execution_count"] = execution_count;
            m_publisher(
                get_request_context(),
                "execute_input",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publish_execution_result(int execution_count, json data, json metadata)
    {
        if (m_publisher)
        {
            json content;
            content["execution_count"] = execution_count;
            content["data"] = std::move(data);
            content["metadata"] = std::move(metadata);
            m_publisher(
                get_request_context(),
                "execute_result",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publish_execution_error(const std::string& ename,
        const std::string& evalue,
        const std::vector<std::string>& trace_back)
    {
        if (m_publisher)
        {
            json content;
            content["ename"] = ename;
            content["evalue"] = evalue;
            content["traceback"] = trace_back;
            m_publisher(
                get_request_context(),
                "error",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::clear_output(bool wait)
    {
        if (m_publisher)
        {
            json content;
            content["wait"] = wait;
            m_publisher(
                get_request_context(),
                "clear_output",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::register_stdin_sender(const stdin_sender_type& sender)
    {
        m_stdin = sender;
    }

    void Interpreter::register_input_handler(const input_reply_handler_type& handler)
    {
        m_inputReplyHandler = handler;
    }

    void Interpreter::register_comm_manager(datasuite::CommManager* manager)
    {
        p_commManager = manager;
    }

    const json& Interpreter::parent_header() const noexcept
    {

        return get_request_context().header();
    }

    void Interpreter::register_control_messenger(ControlMessenger& messenger)
    {
        p_messenger = &messenger;
    }

    void Interpreter::register_history_manager(const HistoryManager& history)
    {
        p_history = &history;
    }

    const HistoryManager& Interpreter::get_history_manager() const noexcept
    {
        return *p_history;
    }

    ControlMessenger& Interpreter::get_control_messenger()
    {
        return *p_messenger;
    }

    void Interpreter::input_request(const std::string& prompt, bool pwd)
    {
        if (m_stdin)
        {
            json content;
            content["prompt"] = prompt;
            content["password"] = pwd;
            m_stdin(
                get_request_context(),
                "input_request",
                json::object(),
                std::move(content)
            );
        }
    }

    void Interpreter::input_reply(const std::string& value)
    {
        if (m_inputReplyHandler)
        {
            m_inputReplyHandler(value);
        }
    }

    json Interpreter::internal_request_impl(const json&)
    {
        json res;
        res["status"] = "error";
        res["what"] = "internal request not supported";
        return res;
    }

    json Interpreter::build_display_content(json data, json metadata, json transient)
    {
        json res;
        res["data"] = std::move(data);
        res["metadata"] = std::move(metadata);
        res["transient"] = std::move(transient);
        return res;
    }

    void Interpreter::set_request_context(RequestContext context)
    {
        m_requestContext = std::move(context);
    }

    const RequestContext& Interpreter::get_request_context() const noexcept
    {
        return m_requestContext;
    }
}
