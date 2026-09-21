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

    private:

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
