#ifndef CARPO_INTERPRETER_PY_HPP
#define CARPO_INTERPRETER_PY_HPP

#include <atomic>
#include <string>
#include <thread>

#include "adrastea/interpreter.hpp"
#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"

namespace carpo
{
    // A real, working Python kernel on top of Adrastea, proving out the
    // *shape* elara::RInterpreter established (native/src/elara/r/
    // interpreter_r.cpp) generalizes to a second language backend -- see
    // native/src/carpo/py/py_dynlib.hpp for how Python's C API is loaded
    // (dynamically, at runtime, mirroring native/src/elara/r/r_dynlib.hpp)
    // and native/src/carpo/interpreter_py.cpp's file comment for the overall
    // design (a small Python-side bootstrap module, hera's equivalent,
    // handling execute/is-complete logic in Python itself).
    //
    // executeRequestImpl, isCompleteRequestImpl, completeRequestImpl, and
    // inspectRequestImpl are all real, backed by a small Python-side
    // bootstrap module (interpreter_py.cpp's kBootstrapSource) using only
    // the standard library (ast/contextlib/traceback/codeop/rlcompleter/
    // inspect) -- no third-party dependency (no jedi) needed.
    class ADRASTEA_API PyInterpreter : public adrastea::Interpreter
    {
    public:
        using base_type = adrastea::Interpreter;

        PyInterpreter() = delete;
        PyInterpreter(int argc, char* argv[]);

        virtual ~PyInterpreter();

        PyInterpreter(const PyInterpreter&) = delete;
        PyInterpreter& operator=(const PyInterpreter&) = delete;

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

    private:
        // Releases this instance's own references to the bootstrap
        // functions and, if this instance is the one that called
        // Py_Initialize() in the first place, finalizes the interpreter.
        // Called from both shutdownRequestImpl() (the normal path) and the
        // destructor (a safety net if shutdown_request was never sent) --
        // idempotent, so whichever runs first does the real work and the
        // other is a no-op.
        void finalizeIfOwned();

        // PyObject* -- kept as void* here so this header (and everything
        // that includes it) never needs to see py_dynlib.hpp's PyObject
        // declaration. Ownership noted per member.
        void* m_userGlobals;        // borrowed (owned by the __main__ module; outlives us)
        void* m_bootstrapRunFn;     // owned (one strong ref), null after finalizeIfOwned()
        void* m_bootstrapIsCompleteFn; // owned (one strong ref), null after finalizeIfOwned()
        void* m_bootstrapCompleteFn;   // owned (one strong ref), null after finalizeIfOwned()
        void* m_bootstrapInspectFn;    // owned (one strong ref), null after finalizeIfOwned()
        void* m_bootstrapEvalExprFn;   // owned (one strong ref), null after finalizeIfOwned()
        std::string m_languageVersion;
        bool m_ownsInterpreter;

        // The thread Python was initialized on (the kernel's main thread,
        // where all code runs): KeyboardInterrupt is only ever raised
        // there, and on POSIX interrupting a blocking call like time.sleep()
        // needs a real SIGINT delivered to exactly that thread.
        std::thread::native_handle_type m_mainThread;
        // True while executeRequestImpl() runs; read from the control
        // thread by interruptRequestImpl().
        std::atomic<bool> m_executing{ false };
        bool m_finalized;
    };

    PyInterpreter* getPyInterpreter();
}

#endif
