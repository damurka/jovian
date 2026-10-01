#ifndef CALLISTO_INTERPRETER_STATA_HPP
#define CALLISTO_INTERPRETER_STATA_HPP

#include <atomic>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

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

        // Compiles Callisto's Mata library (stata/stata_mata.hpp) into a folder
        // of the kernel's own and puts it on the adopath. False if Stata refused.
        bool installMataLibrary();

        // What a function of the Mata library answers: `call` (with @OUT@ for
        // the file it writes its JSON to) run at the prompt, its JSON read.
        // Nothing when it fails. The library is installed again if Mata no
        // longer finds it (the user took its folder off the adopath).
        std::optional<adrastea::json> mataJson(const std::string& call);

        // Names of a kind (callisto_list() in the Mata library): "variables",
        // "globals", "locals", "scalars", "r()", "e()", "s()", "graphs",
        // "adopath". Empty when they cannot be read.
        std::vector<std::string> mataNames(const std::string& kind);

        // The dataset in memory (`.callisto_dataset`): its frame, size, file,
        // variables and value labels.
        adrastea::json describeDataset();

        // Observations of the dataset (`.callisto_data`, `request` its JSON
        // parameters: start, count, variables, formatted). Raw values come
        // through Callisto's plugin (callisto_stata.plugin), else from Mata;
        // formatted ones, as Stata's Data Editor shows them, from Mata.
        adrastea::json readData(const adrastea::json& request);

        // The values of `variables`, observations `first` to `last`, read by
        // the plugin; nothing when it is not there or fails.
        std::optional<adrastea::json> pluginRows(const std::vector<std::string>& variables, long long first, long long last);

        // After a cell that browsed: asks the host to open its data viewer at
        // the variables browse named (a "viewData" question, as hera's View()
        // asks); says so in the output when the host has none.
        void askToViewData();

        // The user_expressions of an execution: `.callisto_dataset` and
        // `.callisto_data` are answered by the kernel (JSON), any other is
        // shown with Stata's display.
        adrastea::json evaluateUserExpressions(const adrastea::json& expressions);

        // The commands for completing a command's first word: Stata's
        // built-in ones, the ado-files on the adopath (read once, again after
        // a cell that may install some) and the programs defined now.
        std::vector<std::string> commandNames();

        // display_data for every graph the last execution drew.
        void publishGraphs();

        // Shuts Stata down once, from shutdown_request or the destructor.
        void shutdownStata();

        std::string m_edition;       // "MP", "SE", "BE"
        std::string m_version;       // c(stata_version), e.g. "19.5"
        std::atomic<bool> m_executing{ false };
        bool m_shutdown = false;
        int m_tempFileCounter = 0;
        std::vector<std::string> m_adoCommands;
        bool m_adoCommandsRead = false;
        std::filesystem::path m_libraryDir;   // lcallisto.mlib
        std::filesystem::path m_pluginPath;   // callisto_stata.plugin, empty when not found
    };
}

#endif
