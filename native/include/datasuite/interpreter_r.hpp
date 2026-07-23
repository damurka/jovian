#ifndef DATASUITE_R_INTERPRETER_HPP
#define DATASUITE_R_INTERPRETER_HPP

#include <string>
#include <memory>

#include "interpreter.hpp"
#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
    class DATASUITE_API RInterpreter : public Interpreter
    {
    public:
        using base_type = Interpreter;

        RInterpreter() = delete;
		RInterpreter(int argc, char* argv[]);

        virtual ~RInterpreter() = default;

        std::stringstream capture_stream;

    protected:

        void configureImpl() override;

        void executeRequestImpl(
            send_reply_callback cb,
            int execution_counter,
            const std::string& code,
            ExecuteRequestConfig config,
            json user_expressions) override;

        json completeRequestImpl(const std::string& code, int cursor_pos) override;

        json inspectRequestImpl(const std::string& code,
            int cursor_pos,
            int detail_level) override;

        json isCompleteRequestImpl(const std::string& code) override;

        json kernelInfoRequestImpl() override;

        json shutdownRequestImpl(bool restart) override;

        json interruptRequestImpl() override;
    };

    RInterpreter* getRInterpreter();
    void registerRRoutines();
}

#endif
