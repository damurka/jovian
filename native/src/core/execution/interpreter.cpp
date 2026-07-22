#include <string>
#include <utility>
#include <vector>

#include "datasuite/json.hpp"
#include "datasuite/interpreter.hpp"

namespace datasuite
{
    interpreter::interpreter()
        : m_executionCount(0)
    {
    }

    void interpreter::configure()
    {
        configure_impl();
    }

    void interpreter::execute_request(RequestContext context,
        send_reply_callback callback,
        const std::string& code,
        execute_request_config config,
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

    json interpreter::complete_request(const std::string& code, int cursor_pos)
    {
        return complete_request_impl(code, cursor_pos);
    }

    json interpreter::inspect_request(const std::string& code, int cursor_pos, int detail_level)
    {
        return inspect_request_impl(code, cursor_pos, detail_level);
    }

    json interpreter::is_complete_request(const std::string& code)
    {
        return is_complete_request_impl(code);
    }

    json interpreter::kernel_info_request()
    {
        return kernel_info_request_impl();
    }

    json interpreter::shutdown_request(bool restart)
    {
        return shutdown_request_impl(restart);
    }

    json interpreter::interrupt_request()
    {
        return interrupt_request_impl();
    }

    json interpreter::internal_request(const json& message)
    {
        return internal_request_impl(message);
    }

    void interpreter::register_publisher(const publisher_type& publisher)
    {
        m_publisher = publisher;
    }

    void interpreter::publish_stream(const std::string& name, const std::string& text)
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

    void interpreter::display_data(json data, json metadata, json transient)
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

    void interpreter::update_display_data(json data, json metadata, json transient)
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

    void interpreter::publish_execution_input(const std::string& code, int execution_count)
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

    void interpreter::publish_execution_result(int execution_count, json data, json metadata)
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

    void interpreter::publish_execution_error(const std::string& ename,
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

    void interpreter::clear_output(bool wait)
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

    void interpreter::register_stdin_sender(const stdin_sender_type& sender)
    {
        m_stdin = sender;
    }

    void interpreter::register_input_handler(const input_reply_handler_type& handler)
    {
        m_inputReplyHandler = handler;
    }

    void interpreter::register_comm_manager(datasuite::comm_manager* manager)
    {
        p_commManager = manager;
    }

    const json& interpreter::parent_header() const noexcept
    {

        return get_request_context().header();
    }

    void interpreter::register_control_messenger(control_messenger& messenger)
    {
        p_messenger = &messenger;
    }

    void interpreter::register_history_manager(const HistoryManager& history)
    {
        p_history = &history;
    }

    const HistoryManager& interpreter::get_history_manager() const noexcept
    {
        return *p_history;
    }

    control_messenger& interpreter::get_control_messenger()
    {
        return *p_messenger;
    }

    void interpreter::input_request(const std::string& prompt, bool pwd)
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

    void interpreter::input_reply(const std::string& value)
    {
        if (m_inputReplyHandler)
        {
            m_inputReplyHandler(value);
        }
    }

    json interpreter::internal_request_impl(const json&)
    {
        json res;
        res["status"] = "error";
        res["what"] = "internal request not supported";
        return res;
    }

    json interpreter::build_display_content(json data, json metadata, json transient)
    {
        json res;
        res["data"] = std::move(data);
        res["metadata"] = std::move(metadata);
        res["transient"] = std::move(transient);
        return res;
    }

    void interpreter::set_request_context(RequestContext context)
    {
        m_requestContext = std::move(context);
    }

    const RequestContext& interpreter::get_request_context() const noexcept
    {
        return m_requestContext;
    }
}
