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
        configureImpl();
    }

    void Interpreter::executeRequest(RequestContext context,
        send_reply_callback callback,
        const std::string& code,
        ExecuteRequestConfig config,
        json user_expressions)
    {
        setRequestContext(std::move(context));
        if (!config.silent)
        {
            ++m_executionCount;
            publishExecutionInput(code, m_executionCount);
        }
        // copy m_executionCount in a local variable to capture it in the lambda
        auto execution_count = m_executionCount;

        auto callback_impl = [execution_count, callback = std::move(callback)](json reply)
            {
                reply["execution_count"] = execution_count;
                callback(std::move(reply));
            };

        executeRequestImpl(
            std::move(callback_impl),
            m_executionCount,
            code,
            std::move(config),
            user_expressions
        );
    }

    json Interpreter::completeRequest(const std::string& code, int cursor_pos)
    {
        return completeRequestImpl(code, cursor_pos);
    }

    json Interpreter::inspectRequest(const std::string& code, int cursor_pos, int detail_level)
    {
        return inspectRequestImpl(code, cursor_pos, detail_level);
    }

    json Interpreter::isCompleteRequest(const std::string& code)
    {
        return isCompleteRequestImpl(code);
    }

    json Interpreter::kernelInfoRequest()
    {
        return kernelInfoRequestImpl();
    }

    json Interpreter::shutdownRequest(bool restart)
    {
        return shutdownRequestImpl(restart);
    }

    json Interpreter::interruptRequest()
    {
        return interruptRequestImpl();
    }

    json Interpreter::internalRequest(const json& message)
    {
        return internalRequestImpl(message);
    }

    void Interpreter::registerPublisher(const publisher_type& publisher)
    {
        m_publisher = publisher;
    }

    void Interpreter::publishStream(const std::string& name, const std::string& text)
    {
        if (m_publisher)
        {
            json content;
            content["name"] = name;
            content["text"] = text;
            m_publisher(
                getRequestContext(),
                "stream",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::displayData(json data, json metadata, json transient)
    {
        if (m_publisher)
        {
            m_publisher(
                getRequestContext(),
                "display_data",
                json::object(),
                buildDisplayContent(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void Interpreter::updateDisplayData(json data, json metadata, json transient)
    {
        if (m_publisher)
        {
            m_publisher(
                getRequestContext(),
                "update_display_data",
                json::object(),
                buildDisplayContent(std::move(data), std::move(metadata), std::move(transient)),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publishExecutionInput(const std::string& code, int execution_count)
    {
        if (m_publisher)
        {
            json content;
            content["code"] = code;
            content["execution_count"] = execution_count;
            m_publisher(
                getRequestContext(),
                "execute_input",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publishExecutionResult(int execution_count, json data, json metadata)
    {
        if (m_publisher)
        {
            json content;
            content["execution_count"] = execution_count;
            content["data"] = std::move(data);
            content["metadata"] = std::move(metadata);
            m_publisher(
                getRequestContext(),
                "execute_result",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::publishExecutionError(const std::string& ename,
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
                getRequestContext(),
                "error",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::clearOutput(bool wait)
    {
        if (m_publisher)
        {
            json content;
            content["wait"] = wait;
            m_publisher(
                getRequestContext(),
                "clear_output",
                json::object(),
                std::move(content),
                buffer_sequence()
            );
        }
    }

    void Interpreter::registerStdinSender(const stdin_sender_type& sender)
    {
        m_stdin = sender;
    }

    void Interpreter::registerInputHandler(const input_reply_handler_type& handler)
    {
        m_inputReplyHandler = handler;
    }

    void Interpreter::registerCommManager(datasuite::CommManager* manager)
    {
        p_commManager = manager;
    }

    const json& Interpreter::parentHeader() const noexcept
    {

        return getRequestContext().header();
    }

    void Interpreter::registerControlMessenger(ControlMessenger& messenger)
    {
        p_messenger = &messenger;
    }

    void Interpreter::registerHistoryManager(const HistoryManager& history)
    {
        p_history = &history;
    }

    const HistoryManager& Interpreter::getHistoryManager() const noexcept
    {
        return *p_history;
    }

    ControlMessenger& Interpreter::getControlMessenger()
    {
        return *p_messenger;
    }

    void Interpreter::inputRequest(const std::string& prompt, bool pwd)
    {
        if (m_stdin)
        {
            json content;
            content["prompt"] = prompt;
            content["password"] = pwd;
            m_stdin(
                getRequestContext(),
                "input_request",
                json::object(),
                std::move(content)
            );
        }
    }

    void Interpreter::inputReply(const std::string& value)
    {
        if (m_inputReplyHandler)
        {
            m_inputReplyHandler(value);
        }
    }

    json Interpreter::internalRequestImpl(const json&)
    {
        json res;
        res["status"] = "error";
        res["what"] = "internal request not supported";
        return res;
    }

    json Interpreter::buildDisplayContent(json data, json metadata, json transient)
    {
        json res;
        res["data"] = std::move(data);
        res["metadata"] = std::move(metadata);
        res["transient"] = std::move(transient);
        return res;
    }

    void Interpreter::setRequestContext(RequestContext context)
    {
        m_requestContext = std::move(context);
    }

    const RequestContext& Interpreter::getRequestContext() const noexcept
    {
        return m_requestContext;
    }
}
