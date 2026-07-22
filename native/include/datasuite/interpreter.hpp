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

    DATASUITE_API bool register_interpreter(Interpreter* interpreter);
    DATASUITE_API Interpreter& get_interpreter();

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
        void execute_request(RequestContext context,
            send_reply_callback callback,
            const std::string& code,
            ExecuteRequestConfig config,
            json user_expressions);

        json complete_request(const std::string& code, int cursor_pos);

        json inspect_request(const std::string& code, int cursor_pos, int detail_level);

        json is_complete_request(const std::string& code);
        json kernel_info_request();

        json shutdown_request(bool restart);
        json interrupt_request();

        json internal_request(const json& message);

        // publish(msg_type, metadata, content)
        using publisher_type = std::function<void(RequestContext, const std::string&, json, json, buffer_sequence)>;
        void register_publisher(const publisher_type& publisher);

        void publish_stream(const std::string& name, const std::string& text);
        void display_data(json data, json metadata, json transient);
        void update_display_data(json data, json metadata, json transient);
        void publish_execution_input(const std::string& code, int execution_count);
        void publish_execution_result(int execution_count, json data, json metadata);
        void publish_execution_error(const std::string& ename,
            const std::string& evalue,
            const std::vector<std::string>& trace_back);
        void clear_output(bool wait);

        // send_stdin(msg_type, metadata, content)
        using stdin_sender_type = std::function<void(RequestContext, const std::string&, json, json)>;
        void register_stdin_sender(const stdin_sender_type& sender);
        using input_reply_handler_type = std::function<void(const std::string&)>;
        void register_input_handler(const input_reply_handler_type& handler);

        void input_request(const std::string& prompt, bool pwd);
        void input_reply(const std::string& value);

        void register_comm_manager(datasuite::CommManager* manager);

        // --- FIXED NAMING COLLISIONS HERE ---
        datasuite::CommManager& get_comm_manager() noexcept;
        const datasuite::CommManager& get_comm_manager() const noexcept;

        const json& parent_header() const noexcept;

        void register_control_messenger(ControlMessenger& messenger);

        void register_history_manager(const HistoryManager& history);
        const HistoryManager& get_history_manager() const noexcept;

    protected:

        ControlMessenger& get_control_messenger();

    private:

        virtual void configure_impl() = 0;

        virtual void execute_request_impl(send_reply_callback cb,
            int execution_counter,
            const std::string& code,
            ExecuteRequestConfig config,
            json user_expressions) = 0;

        virtual json complete_request_impl(const std::string& code,
            int cursor_pos) = 0;

        virtual json inspect_request_impl(const std::string& code,
            int cursor_pos,
            int detail_level) = 0;

        virtual json is_complete_request_impl(const std::string& code) = 0;

        virtual json kernel_info_request_impl() = 0;

        virtual json shutdown_request_impl(bool restart) = 0;
        virtual json interrupt_request_impl() = 0;

        virtual json internal_request_impl(const json& message);

        json build_display_content(json data, json metadata, json transient);

        virtual void set_request_context(RequestContext context);
        virtual const RequestContext& get_request_context() const noexcept;

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
    inline datasuite::CommManager& Interpreter::get_comm_manager() noexcept
    {
        return *p_commManager;
    }

    inline const datasuite::CommManager& Interpreter::get_comm_manager() const noexcept
    {
        return *p_commManager;
    }
}

#endif
