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
    class interpreter;

    DATASUITE_API bool register_interpreter(interpreter* interpreter);
    DATASUITE_API interpreter& get_interpreter();

    struct DATASUITE_API execute_request_config
    {
        bool silent;
        bool store_history;
        bool allow_stdin;
    };

    class DATASUITE_API interpreter
    {
    public:

        interpreter();
        virtual ~interpreter() = default;

        interpreter(const interpreter&) = delete;
        interpreter& operator=(const interpreter&) = delete;

        interpreter(interpreter&&) = delete;
        interpreter& operator=(interpreter&&) = delete;

        void configure();

        using send_reply_callback = std::function<void(nl::json)>;
        void execute_request(RequestContext context,
            send_reply_callback callback,
            const std::string& code,
            execute_request_config config,
            nl::json user_expressions);

        nl::json complete_request(const std::string& code, int cursor_pos);

        nl::json inspect_request(const std::string& code, int cursor_pos, int detail_level);

        nl::json is_complete_request(const std::string& code);
        nl::json kernel_info_request();

        nl::json shutdown_request(bool restart);
        nl::json interrupt_request();

        nl::json internal_request(const nl::json& message);

        // publish(msg_type, metadata, content)
        using publisher_type = std::function<void(RequestContext, const std::string&, nl::json, nl::json, buffer_sequence)>;
        void register_publisher(const publisher_type& publisher);

        void publish_stream(const std::string& name, const std::string& text);
        void display_data(nl::json data, nl::json metadata, nl::json transient);
        void update_display_data(nl::json data, nl::json metadata, nl::json transient);
        void publish_execution_input(const std::string& code, int execution_count);
        void publish_execution_result(int execution_count, nl::json data, nl::json metadata);
        void publish_execution_error(const std::string& ename,
            const std::string& evalue,
            const std::vector<std::string>& trace_back);
        void clear_output(bool wait);

        // send_stdin(msg_type, metadata, content)
        using stdin_sender_type = std::function<void(RequestContext, const std::string&, nl::json, nl::json)>;
        void register_stdin_sender(const stdin_sender_type& sender);
        using input_reply_handler_type = std::function<void(const std::string&)>;
        void register_input_handler(const input_reply_handler_type& handler);

        void input_request(const std::string& prompt, bool pwd);
        void input_reply(const std::string& value);

        void register_comm_manager(datasuite::comm_manager* manager);

        // --- FIXED NAMING COLLISIONS HERE ---
        datasuite::comm_manager& get_comm_manager() noexcept;
        const datasuite::comm_manager& get_comm_manager() const noexcept;

        const nl::json& parent_header() const noexcept;

        void register_control_messenger(control_messenger& messenger);

        void register_history_manager(const HistoryManager& history);
        const HistoryManager& get_history_manager() const noexcept;

    protected:

        control_messenger& get_control_messenger();

    private:

        virtual void configure_impl() = 0;

        virtual void execute_request_impl(send_reply_callback cb,
            int execution_counter,
            const std::string& code,
            execute_request_config config,
            nl::json user_expressions) = 0;

        virtual nl::json complete_request_impl(const std::string& code,
            int cursor_pos) = 0;

        virtual nl::json inspect_request_impl(const std::string& code,
            int cursor_pos,
            int detail_level) = 0;

        virtual nl::json is_complete_request_impl(const std::string& code) = 0;

        virtual nl::json kernel_info_request_impl() = 0;

        virtual nl::json shutdown_request_impl(bool restart) = 0;
        virtual nl::json interrupt_request_impl() = 0;

        virtual nl::json internal_request_impl(const nl::json& message);

        nl::json build_display_content(nl::json data, nl::json metadata, nl::json transient);

        virtual void set_request_context(RequestContext context);
        virtual const RequestContext& get_request_context() const noexcept;

        publisher_type m_publisher;
        stdin_sender_type m_stdin;
        int m_executionCount;
        datasuite::comm_manager* p_commManager;
        input_reply_handler_type m_inputReplyHandler;
        control_messenger* p_messenger;
        const HistoryManager* p_history;
        RequestContext m_requestContext;
    };

    // --- FIXED INLINE DEFINITIONS HERE ---
    inline datasuite::comm_manager& interpreter::get_comm_manager() noexcept
    {
        return *p_commManager;
    }

    inline const datasuite::comm_manager& interpreter::get_comm_manager() const noexcept
    {
        return *p_commManager;
    }
}

#endif