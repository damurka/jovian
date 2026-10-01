#ifndef ELARA_R_INTERPRETER_HPP
#define ELARA_R_INTERPRETER_HPP

#include <atomic>
#include <sstream>
#include <thread>
#include <string>
#include <memory>

#include "adrastea/interpreter.hpp"
#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"
#include "adrastea/server.hpp"
#include <vector>

namespace elara
{
    using SEXP_t = void*; // an R SEXP (R's headers are not included here)

    class ADRASTEA_API RInterpreter : public adrastea::Interpreter
    {
    public:
        using base_type = adrastea::Interpreter;

        RInterpreter() = delete;
		RInterpreter(int argc, char* argv[]);

        virtual ~RInterpreter() = default;

        std::stringstream capture_stream;

        // R's own console loop (run_Rmainloop()) owns the main thread, as in Ark: the server's loop runs inside
        // readConsole() whenever R waits for top-level input (see Server::setMainLoop()).
        void attachServer(adrastea::Server* server);
        void runMainLoop();
        int readConsole(const char* prompt, unsigned char* buffer, int length);
        void writeConsole(const char* buf, int buflen, int otype);

        // hera's error handler (.jv.errors.handler): whether an error is the running cell's, and its report
        bool wantsCellError() const;

        // an interrupt (interrupt_request) is pending
        bool interruptRequested() const { return m_interruptRequested.load(); }
        // the debugger (debugger_r.cpp): whether a requested pause is due (hera's interrupt handler asks)
        bool takeDebugPause();
        void recordCellError(std::string evalue, std::vector<std::string> traceback);

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
        adrastea::json debugRequestImpl(const adrastea::json& request) override;

        void idleImpl() override;

    private:
        // A cell being run: its parsed expressions, evaluated one per turn of R's console loop
        struct Cell
        {
            send_reply_callback reply;
            int executionCount = 0;
            bool silent = false;
            adrastea::json userExpressions;
            SEXP_t exprs = nullptr; // kept alive in base::.jv_cell (see readConsole())
            long next = 0;
            long count = 0;
            bool checkGraphics = false;
            bool failed = false;
            bool errorRecorded = false;
            std::string evalue;
            std::vector<std::string> traceback;
            std::string valueText; // what R's autoprint wrote for the last expression
        };

        int stdinInput(const char* prompt, unsigned char* buffer, int length);
        bool evaluateNext(unsigned char* buffer, int length);
        void finishCell();

        adrastea::Server* m_server = nullptr;
        std::unique_ptr<class RDebugger> m_debugger;
        std::unique_ptr<Cell> m_cell;
        bool m_evaluating = false;  // a cell's expression is being evaluated by readConsole()
        bool m_loopStarted = false; // R's console loop has run its start-up line (see readConsole())
        std::string m_topPrompt = "> "; // R's top-level prompt (getOption("prompt")), as of the cell's start
        std::string m_continuePrompt = "+ "; // and its continuation prompt (getOption("continue"))
        std::string m_debugOutput;       // stdout while debugging, a line at a time (see writeConsole())
        std::string m_pendingLine;       // the line for R's console once it has reset its input (evaluateNext())
        bool m_pendingTail = false;
        bool m_pendingCapture = false;
        bool m_inTail = false;      // R's console prints the value, warnings, ... of the expression just evaluated
        bool m_captureValue = false; // ... and it was the cell's last: its printed value is the cell's result

        // The parsed idle expression (see idleImpl()), preserved from R's
        // garbage collector; null until first needed.
        void* m_idleExpression = nullptr;
        // Set once R has been shut down: nothing may call into it after.
        bool m_rEnded = false;

        // True while executeRequestImpl() is running; read from the control
        // thread by interruptRequestImpl().
        std::atomic<bool> m_executing{ false };
        // Set when an interrupt_request arrived during the current execution,
        // so an execution R unwinds out of can be reported as interrupted.
        std::atomic<bool> m_interruptRequested{ false };
        // The thread R runs on (POSIX): blocking calls such as Sys.sleep()
        // only notice the interrupt flag once a signal wakes them.
        std::thread::native_handle_type m_mainThread{};
    };

    RInterpreter* getRInterpreter();
    void registerRRoutines();
}

#endif
