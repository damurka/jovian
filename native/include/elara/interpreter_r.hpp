#ifndef ELARA_R_INTERPRETER_HPP
#define ELARA_R_INTERPRETER_HPP

#include <string>
#include <memory>

#include "adrastea/interpreter.hpp"
#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"

namespace elara
{
    class ADRASTEA_API RInterpreter : public adrastea::Interpreter
    {
    public:
        using base_type = adrastea::Interpreter;

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
            adrastea::ExecuteRequestConfig config,
            adrastea::json user_expressions) override;

        adrastea::json completeRequestImpl(const std::string& code, int cursor_pos) override;

        adrastea::json inspectRequestImpl(const std::string& code,
            int cursor_pos,
            int detail_level) override;

        adrastea::json isCompleteRequestImpl(const std::string& code) override;

        adrastea::json kernelInfoRequestImpl() override;

        adrastea::json shutdownRequestImpl(bool restart) override;

        adrastea::json interruptRequestImpl() override;
    };

    RInterpreter* getRInterpreter();
    void registerRRoutines();
}

#endif
