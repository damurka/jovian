#ifndef DATASUITE_R_INTERPRETER_HPP
#define DATASUITE_R_INTERPRETER_HPP

#include <string>
#include <memory>

#include "nlohmann/json.hpp"

#include "interpreter.hpp"
#include "datasuite.hpp"

namespace nl = nlohmann;

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
            nl::json user_expressions) override;

        nl::json complete_request_impl(const std::string& code, int cursor_pos) override;

        nl::json inspect_request_impl(const std::string& code,
            int cursor_pos,
            int detail_level) override;

        nl::json is_complete_request_impl(const std::string& code) override;

        nl::json kernel_info_request_impl() override;

        nl::json shutdown_request_impl(bool restart) override;

        nl::json interrupt_request_impl() override;
    };

    r_interpreter* get_r_interpreter();
    void register_r_routines();
}

#endif