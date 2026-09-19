#ifndef ELARA_R_INTERPRETER_HPP
#define ELARA_R_INTERPRETER_HPP

#include <string>
#include <memory>

#include "adrastea/interpreter.hpp"
#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"

namespace elara
{
    // Elara builds on the Adrastea framework; name its symbols unqualified here.
    using namespace adrastea;

    class ADRASTEA_API RInterpreter : public Interpreter
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
