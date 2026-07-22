#ifndef DATASUITE_R_INTERPRETER_HPP
#define DATASUITE_R_INTERPRETER_HPP

#include <string>
#include <memory>

#include "interpreter.hpp"
#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
    class DATASUITE_API r_interpreter : public interpreter
    {
    public:
        using base_type = interpreter;

        r_interpreter() = delete;
		r_interpreter(int argc, char* argv[]);

        virtual ~r_interpreter() = default;

        std::stringstream capture_stream;

    protected:

        void configure_impl() override;

        void execute_request_impl(
            send_reply_callback cb,
            int execution_counter,
            const std::string& code,
            execute_request_config config,
            json user_expressions) override;

        json complete_request_impl(const std::string& code, int cursor_pos) override;

        json inspect_request_impl(const std::string& code,
            int cursor_pos,
            int detail_level) override;

        json is_complete_request_impl(const std::string& code) override;

        json kernel_info_request_impl() override;

        json shutdown_request_impl(bool restart) override;

        json interrupt_request_impl() override;
    };

    r_interpreter* get_r_interpreter();
    void register_r_routines();
}

#endif