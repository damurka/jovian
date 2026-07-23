#ifndef DATASUITE_INTERPRETER_HPP
#define DATASUITE_INTERPRETER_HPP

#include <functional>
#include <string>
#include <vector>

#include "comm.hpp"
#include "datasuite.hpp"
#include "control_messenger.hpp"
#include "history_manager.hpp"
#include "request_context.hpp"

namespace datasuite
{
    class Interpreter;

    DATASUITE_API bool registerInterpreter(Interpreter* interpreter);
    DATASUITE_API Interpreter& getInterpreter();

    struct DATASUITE_API ExecuteRequestConfig
    {
        bool silent;
        bool store_history;
        bool allow_stdin;
    };

    class DATASUITE_API Interpreter
    {
    public:

        Interpreter();
        virtual ~Interpreter() = default;

        Interpreter(const Interpreter&) = delete;
        Interpreter& operator=(const Interpreter&) = delete;

        Interpreter(Interpreter&&) = delete;
        Interpreter& operator=(Interpreter&&) = delete;

        void configure();

        using send_reply_callback = std::function<void(json)>;
        void executeRequest(RequestContext context,
            send_reply_callback callback,
            const std::string& code,
            ExecuteRequestConfig config,
            json user_expressions);

        json completeRequest(const std::string& code, int cursor_pos);

        json inspectRequest(const std::string& code, int cursor_pos, int detail_level);

        json isCompleteRequest(const std::string& code);
        json kernelInfoRequest();

        json shutdownRequest(bool restart);
        json interruptRequest();

        json internalRequest(const json& message);

        // publish(msg_type, metadata, content)
        using publisher_type = std::function<void(RequestContext, const std::string&, json, json, buffer_sequence)>;
        void registerPublisher(const publisher_type& publisher);

        void publishStream(const std::string& name, const std::string& text);
        void displayData(json data, json metadata, json transient);
        void updateDisplayData(json data, json metadata, json transient);
        void publishExecutionInput(const std::string& code, int execution_count);
        void publishExecutionResult(int execution_count, json data, json metadata);
        void publishExecutionError(const std::string& ename,
            const std::string& evalue,
            const std::vector<std::string>& trace_back);
        void clearOutput(bool wait);

        // sendStdin(msg_type, metadata, content)
        using stdin_sender_type = std::function<void(RequestContext, const std::string&, json, json)>;
        void registerStdinSender(const stdin_sender_type& sender);
        using input_reply_handler_type = std::function<void(const std::string&)>;
        void registerInputHandler(const input_reply_handler_type& handler);

        void inputRequest(const std::string& prompt, bool pwd);
        void inputReply(const std::string& value);

        void registerCommManager(datasuite::CommManager* manager);

        // --- FIXED NAMING COLLISIONS HERE ---
        datasuite::CommManager& getCommManager() noexcept;
        const datasuite::CommManager& getCommManager() const noexcept;

        const json& parentHeader() const noexcept;

        void registerControlMessenger(ControlMessenger& messenger);

        void registerHistoryManager(const HistoryManager& history);
        const HistoryManager& getHistoryManager() const noexcept;

    protected:

        ControlMessenger& getControlMessenger();

    private:

        virtual void configureImpl() = 0;

        virtual void executeRequestImpl(send_reply_callback cb,
            int execution_counter,
            const std::string& code,
            ExecuteRequestConfig config,
            json user_expressions) = 0;

        virtual json completeRequestImpl(const std::string& code,
            int cursor_pos) = 0;

        virtual json inspectRequestImpl(const std::string& code,
            int cursor_pos,
            int detail_level) = 0;

        virtual json isCompleteRequestImpl(const std::string& code) = 0;

        virtual json kernelInfoRequestImpl() = 0;

        virtual json shutdownRequestImpl(bool restart) = 0;
        virtual json interruptRequestImpl() = 0;

        virtual json internalRequestImpl(const json& message);

        json buildDisplayContent(json data, json metadata, json transient);

        virtual void setRequestContext(RequestContext context);
        virtual const RequestContext& getRequestContext() const noexcept;

        publisher_type m_publisher;
        stdin_sender_type m_stdin;
        int m_executionCount;
        datasuite::CommManager* p_commManager;
        input_reply_handler_type m_inputReplyHandler;
        ControlMessenger* p_messenger;
        const HistoryManager* p_history;
        RequestContext m_requestContext;
    };

    // --- FIXED INLINE DEFINITIONS HERE ---
    inline datasuite::CommManager& Interpreter::getCommManager() noexcept
    {
        return *p_commManager;
    }

    inline const datasuite::CommManager& Interpreter::getCommManager() const noexcept
    {
        return *p_commManager;
    }
}

#endif
