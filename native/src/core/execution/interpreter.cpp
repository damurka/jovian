#include <string>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

#include "datasuite/interpreter.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    interpreter::interpreter()
        : m_execution_count(0)
    {
    }

    void interpreter::configure()
    {
        configure_impl();
    }

    void interpreter::execute_request(request_context context,
        send_reply_callback callback,
        const std::string& code,
        execute_request_config config,
        nl::json user_expressions)
    {
        set_request_context(std::move(context));
        if (!config.silent)
        {
            ++m_execution_count;
            publish_execution_input(code, m_execution_count);
        }
        // copy m_execution_count in a local variable to capture it in the lambda
        auto execution_count = m_execution_count;

        auto callback_impl = [execution_count, callback = std::move(callback)](nl::json reply)
            {
                reply["execution_count"] = execution_count;
                callback(std::move(reply));
            };

        execute_request_impl(
            std::move(callback_impl),
            m_execution_count,
            code,
            std::move(config),
            user_expressions
        );
    }

    nl::json interpreter::complete_request(const std::string& code, int cursor_pos)
    {
        return complete_request_impl(code, cursor_pos);
    }

    nl::json interpreter::inspect_request(const std::string& code, int cursor_pos, int detail_level)
    {
        return inspect_request_impl(code, cursor_pos, detail_level);
    }

    nl::json interpreter::is_complete_request(const std::string& code)
    {
        return is_complete_request_impl(code);
    }

    nl::json interpreter::kernel_info_request()
    {
        return kernel_info_request_impl();
    }

    nl::json interpreter::shutdown_request(bool restart)
    {
        return shutdown_request_impl(restart);
    }

    nl::json interpreter::interrupt_request()
    {
        return interrupt_request_impl();
    }

    nl::json interpreter::internal_request(const nl::json& message)
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
            nl::json content;
            content["name"] = name;
            content["text"] = text;
            m_publisher(
                get_request_context(),
                "stream",
                nl::json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void interpreter::display_data(nl::json data, nl::json metadata, nl::json transient)
    {
        if (m_publisher)
        {
            m_publisher(
                get_request_context(),
                "display_data",
                nl::json::object(),
                build_display_content(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void interpreter::update_display_data(nl::json data, nl::json metadata, nl::json transient)
    {
        if (m_publisher)
        {
            m_publisher(
                get_request_context(),
                "update_display_data",
                nl::json::object(),
                build_display_content(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void interpreter::publish_execution_input(const std::string& code, int execution_count)
    {
        if (m_publisher)
        {
            nl::json content;
            content["code"] = code;
            content["execution_count"] = execution_count;
            m_publisher(
                get_request_context(),
                "execute_input",
                nl::json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void interpreter::publish_execution_result(int execution_count, nl::json data, nl::json metadata)
    {
        if (m_publisher)
        {
            nl::json content;
            content["execution_count"] = execution_count;
            content["data"] = std::move(data);
            content["metadata"] = std::move(metadata);
            m_publisher(
                get_request_context(),
                "execute_result",
                nl::json::object(),
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
            nl::json content;
            content["ename"] = ename;
            content["evalue"] = evalue;
            content["traceback"] = trace_back;
            m_publisher(
                get_request_context(),
                "error",
                nl::json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void interpreter::clear_output(bool wait)
    {
        if (m_publisher)
        {
            nl::json content;
            content["wait"] = wait;
            m_publisher(
                get_request_context(),
                "clear_output",
                nl::json::object(),
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
        m_input_reply_handler = handler;
    }

    void interpreter::register_comm_manager(datasuite::comm_manager* manager)
    {
        p_comm_manager = manager;
    }

    const nl::json& interpreter::parent_header() const noexcept
    {

        return get_request_context().header();
    }

    void interpreter::register_control_messenger(control_messenger& messenger)
    {
        p_messenger = &messenger;
    }

    void interpreter::register_history_manager(const history_manager& history)
    {
        p_history = &history;
    }

    const history_manager& interpreter::get_history_manager() const noexcept
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
            nl::json content;
            content["prompt"] = prompt;
            content["password"] = pwd;
            m_stdin(
                get_request_context(),
                "input_request",
                nl::json::object(),
                std::move(content)
            );
        }
    }

    void interpreter::input_reply(const std::string& value)
    {
        if (m_input_reply_handler)
        {
            m_input_reply_handler(value);
        }
    }

    nl::json interpreter::internal_request_impl(const nl::json&)
    {
        nl::json res;
        res["status"] = "error";
        res["what"] = "internal request not supported";
        return res;
    }

    nl::json interpreter::build_display_content(nl::json data, nl::json metadata, nl::json transient)
    {
        nl::json res;
        res["data"] = std::move(data);
        res["metadata"] = std::move(metadata);
        res["transient"] = std::move(transient);
        return res;
    }

    void interpreter::set_request_context(request_context context)
    {
        m_request_context = std::move(context);
    }

    const request_context& interpreter::get_request_context() const noexcept
    {
        return m_request_context;
    }
}
