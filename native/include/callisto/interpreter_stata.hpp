#ifndef CALLISTO_INTERPRETER_STATA_HPP
#define CALLISTO_INTERPRETER_STATA_HPP

#include <atomic>
#include <string>

#include "adrastea/interpreter.hpp"
#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"

namespace callisto
{
    // The Stata kernel: embeds Stata (17 or newer) through the shared library
    // Stata ships for pystata -- see native/src/callisto/stata/stata_dynlib.hpp
    // for how it is loaded and native/src/callisto/interpreter_stata.cpp for
    // how requests become Stata commands.
    class ADRASTEA_API StataInterpreter : public adrastea::Interpreter
    {
    public:
        using base_type = adrastea::Interpreter;

        // Reads STATA_HOME and CALLISTO_STATA_EDITION (set by
        // callisto::Server::setupEnvironment()), loads Stata's library and
        // starts Stata. Throws std::runtime_error if Stata cannot be loaded
        // or started (no license, a Stata older than 17, ...).
        StataInterpreter();

        virtual ~StataInterpreter();

        StataInterpreter(const StataInterpreter&) = delete;
        StataInterpreter& operator=(const StataInterpreter&) = delete;

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

        bool answersWhileBusyImpl(const std::string& msg_type) const override;

    private:
        // Runs one command, not streamed, and returns what it printed.
        std::string runCaptured(const std::string& command, int* rc = nullptr);

        // display_data for every graph the last execution drew.
        void publishGraphs();

        // Shuts Stata down once, from shutdown_request or the destructor.
        void shutdownStata();

        std::string m_edition;       // "MP", "SE", "BE"
        std::string m_version;       // c(stata_version), e.g. "19.5"
        std::atomic<bool> m_executing{ false };
        bool m_shutdown = false;
        int m_tempFileCounter = 0;
    };
}

#endif
