#ifndef CARPO_INTERPRETER_PY_HPP
#define CARPO_INTERPRETER_PY_HPP

#include <string>

#include "adrastea/interpreter.hpp"
#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"

namespace carpo
{
    // Scaffolding for a future Python kernel, modeled on Positron's Ark and
    // xeus-python -- NOT a working Python interpreter. Every *RequestImpl()
    // that would need real Python execution returns a clear "not yet
    // implemented" error instead of pretending to work; kernelInfoRequestImpl()
    // is the one exception, fully implemented, so kernel discovery/jupyter_client
    // tooling can see this kernel identify itself correctly even though it
    // can't execute anything yet.
    //
    // This exists to prove out the *shape* elara::RInterpreter established
    // (native/src/elara/r/interpreter_r.cpp) generalizes to a second language
    // backend on top of Adrastea -- see docs/cpp-usage.md's "Writing a new
    // interpreter" section for what a real implementation would replace this
    // with (embedding CPython, likely via xeus-python or a direct CPython C
    // API embedding, the same way RInterpreter embeds R via Rf_initEmbeddedR).
    class ADRASTEA_API PyInterpreter : public adrastea::Interpreter
    {
    public:
        using base_type = adrastea::Interpreter;

        PyInterpreter() = delete;
        PyInterpreter(int argc, char* argv[]);

        virtual ~PyInterpreter() = default;

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

    PyInterpreter* getPyInterpreter();
}

#endif
