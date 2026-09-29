#include "callisto/interpreter_stata.hpp"
#include "adrastea/guid.hpp"
#include "adrastea/helper.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <vector>

#include <openssl/evp.h>

#include "callisto/stata/stata_dynlib.hpp"
#include "callisto/stata/stata_text.hpp"

// Stata embedded through its shared library (stata/stata_dynlib.hpp). Every
// request is answered by running Stata commands:
//
// - execute: the cell is written to a temporary do-file and run with
//   `include`, not `do`, so it runs in the interactive context -- local
//   macros defined in one cell are still there in the next, as they would be
//   typed at Stata's prompt -- and multi-line constructs (/* */, ///, loops,
//   programs, #delimit) work as in any do-file. Stata prints into its output
//   buffer; a second thread drains that buffer every few milliseconds while
//   the command runs and publishes it as stdout (pystata streams output the
//   same way). A non-zero return code is an error whose ename is Stata's
//   "r(<rc>)" and whose evalue is the message printed above it.
// - graphs: `_gr_list on` makes Stata record the graphs a cell draws;
//   afterwards each is exported to a PNG and published as display_data, as
//   pystata's inline graphs do.
// - complete / inspect: variable names and macros come from Mata
//   (st_varname(), st_dir()), which leaves r() alone; inspecting a variable
//   runs describe and summarize between `_return hold` and `_return restore`,
//   so the user's r() results survive.
// - interrupt: StataSO_SetBreak(), Stata's own Break.
namespace callisto
{
    using namespace callisto::stata;

    namespace
    {
        // How much of an execution's output is kept to find its error message.
        constexpr std::size_t kErrorTailBytes = 16 * 1024;
        constexpr auto kOutputPollInterval = std::chrono::milliseconds(20);

        // Everything Stata has printed since the last call (the buffer is
        // emptied by reading it).
        std::string takeOutput()
        {
            const char* chunk = StataSO_GetOutputBuffer();
            return chunk ? std::string(chunk) : std::string();
        }

        // A Stata compound-quoted string: `"..."'.
        std::string quoted(const std::string& value)
        {
            return "`\"" + value + "\"'";
        }

        void setEnv(const char* name, const std::string& value)
        {
#ifdef _WIN32
            _putenv_s(name, value.c_str());
#else
            setenv(name, value.c_str(), 1);
#endif
        }

        std::string base64(const std::string& bytes)
        {
            std::string encoded(4 * ((bytes.size() + 2) / 3) + 1, '\0');
            int length = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()),
                reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()));
            encoded.resize(length > 0 ? static_cast<std::size_t>(length) : 0);
            return encoded;
        }

        std::filesystem::path tempFile(const std::string& extension)
        {
            return std::filesystem::temp_directory_path() / ("callisto-" + adrastea::newGuid().toString() + extension);
        }

        // Forward slashes: Stata reads a backslash before ` or $ as an escape.
        std::string stataPath(const std::filesystem::path& path)
        {
            return path.generic_string();
        }

        std::string upper(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return value;
        }

        adrastea::json errorTraceback(const std::string& ename, const std::string& evalue)
        {
            return adrastea::json::array({ evalue, ename + ";" });
        }
    }

    StataInterpreter::StataInterpreter()
    {
        const char* home = std::getenv("STATA_HOME");
        const char* edition = std::getenv("CALLISTO_STATA_EDITION");
        const std::string stataHome = home ? home : "";
        Library library = loadStataApi(stataHome, edition ? edition : "");
        m_edition = upper(library.edition);

        // Stata's library finds its own files (ado directories, the license)
        // through SYSDIR_STATA, as pystata sets it.
        setEnv("SYSDIR_STATA", stataHome);

        // -q: no splash banner.
        char arg0[] = "callisto";
        char quiet[] = "-q";
        char* argv[] = { arg0, quiet };
        int rc = StataSO_Main(2, argv);
        std::string startup = text::trim(takeOutput());
        // -7100 is Stata saying its Python integration is unavailable, which
        // callisto does not use (pystata treats it as a successful start too).
        if (rc < 0 && rc != -7100)
        {
            std::string message = "Stata (" + library.path + ") could not start: " +
                                  (startup.empty() ? "StataSO_Main returned " + std::to_string(rc) : startup);
            // The edition was a guess (the first installed), and the license
            // may be for another one.
            if ((!edition || !*edition) && library.installed.size() > 1)
            {
                std::string others;
                for (const auto& ed : library.installed)
                {
                    if (ed == library.edition) continue;
                    others += (others.empty() ? "'" : " or '") + ed + "'";
                }
                message += "\n(Stata/" + m_edition + " was loaded because it is the first edition installed there; "
                           "if your license is for another edition, pass stataEdition " + others + ".)";
            }
            throw std::runtime_error(message);
        }

        m_version = text::trim(runCaptured("display c(stata_version)"));
        runCaptured("set more off");

#ifdef _WIN32
        // What pystata does too: Stata's Java-based features need the Java it
        // ships with on PATH.
        std::string javaHome = text::trim(runCaptured("display " + quoted("`c(java_home)'")));
        if (!javaHome.empty())
        {
            const char* path = std::getenv("PATH");
            setEnv("PATH", std::string(path ? path : "") + ";" + javaHome + "\\bin");
        }
#endif

        adrastea::registerInterpreter(this);
    }

    StataInterpreter::~StataInterpreter()
    {
        shutdownStata();
    }

    void StataInterpreter::shutdownStata()
    {
        if (m_shutdown) return;
        m_shutdown = true;
        StataSO_Shutdown();
    }

    std::string StataInterpreter::runCaptured(const std::string& command, int* rc)
    {
        StataSO_ClearOutputBuffer();
        int result = StataSO_Execute(command.c_str(), 0);
        if (rc) *rc = result;
        return takeOutput();
    }

    void StataInterpreter::configureImpl()
    {
        printf("[callisto] StataInterpreter::configureImpl() -- Stata %s (%s) ready\n", m_version.c_str(), m_edition.c_str());
        fflush(stdout);
    }

    void StataInterpreter::executeRequestImpl(
        send_reply_callback cb,
        int /*execution_count*/,
        const std::string& code,
        adrastea::ExecuteRequestConfig config,
        adrastea::json user_expressions)
    {
        if (text::trim(code).empty())
        {
            cb(adrastea::createSuccessfulReply());
            return;
        }

        const std::filesystem::path doFile = tempFile(".do");
        {
            std::ofstream file(doFile, std::ios::binary);
            // A do-file's last line needs its newline.
            file << code << "\n";
            if (!file)
            {
                std::string message = "could not write the cell to " + doFile.string();
                publishExecutionError("CallistoInternalError", message, {});
                cb(adrastea::createErrorReply("CallistoInternalError", message));
                return;
            }
        }

        const bool showGraphs = !config.silent;
        if (showGraphs)
        {
            runCaptured("quietly _gr_list on");
        }

        std::string tail;
        auto publish = [&](const std::string& chunk) {
            if (chunk.empty()) return;
            tail += chunk;
            if (tail.size() > kErrorTailBytes) tail.erase(0, tail.size() - kErrorTailBytes);
            if (!config.silent) publishStream("stdout", chunk);
        };

        StataSO_ClearOutputBuffer();
        std::atomic<bool> finished{ false };
        std::thread poller([&]() {
            while (!finished.load())
            {
                publish(takeOutput());
                std::this_thread::sleep_for(kOutputPollInterval);
            }
        });

        int rc = 0;
        {
            m_executing = true;
            rc = StataSO_Execute(("include " + quoted(stataPath(doFile))).c_str(), 0);
            m_executing = false;
        }
        finished = true;
        poller.join();
        publish(takeOutput());

        std::error_code ec;
        std::filesystem::remove(doFile, ec);

        if (showGraphs)
        {
            publishGraphs();
            runCaptured("quietly _gr_list off");
        }

        if (rc != 0)
        {
            std::string ename = "r(" + std::to_string(rc) + ")";
            std::string evalue = text::errorMessage(tail, rc);
            if (evalue.empty()) evalue = rc == 1 ? "--Break--" : "Stata returned an error";
            publishExecutionError(ename, evalue, errorTraceback(ename, evalue));
            cb(adrastea::createErrorReply(ename, evalue, errorTraceback(ename, evalue)));
            return;
        }

        // Each expression is shown with Stata's display command.
        adrastea::json userExpressionResults = adrastea::json::object();
        if (user_expressions.is_object())
        {
            for (auto it = user_expressions.begin(); it != user_expressions.end(); ++it)
            {
                const std::string expr = it.value().is_string() ? it.value().get<std::string>() : std::string();
                int exprRc = 0;
                std::string output = runCaptured("display " + expr, &exprRc);
                if (exprRc == 0)
                {
                    userExpressionResults[it.key()] = { { "status", "ok" },
                        { "data", { { "text/plain", text::trim(output) } } }, { "metadata", adrastea::json::object() } };
                }
                else
                {
                    userExpressionResults[it.key()] = { { "status", "error" },
                        { "ename", "r(" + std::to_string(exprRc) + ")" },
                        { "evalue", text::errorMessage(output, exprRc) },
                        { "traceback", adrastea::json::array() } };
                }
            }
        }
        cb(adrastea::createSuccessfulReply(adrastea::json::array(), userExpressionResults));
    }

    void StataInterpreter::publishGraphs()
    {
        // `_gr_list list` returns its list in r(); keep the user's r() results.
        runCaptured("capture _return hold __callisto_r");
        runCaptured("quietly _gr_list list");
        std::vector<std::string> names = text::splitNames(runCaptured("display " + quoted("`r(_grlist)'")));
        runCaptured("capture _return restore __callisto_r");

        for (const auto& name : names)
        {
            int rc = 0;
            runCaptured("quietly graph display " + name, &rc);
            if (rc != 0) continue;

            const std::filesystem::path png = tempFile(".png");
            runCaptured("quietly graph export " + quoted(stataPath(png)) + ", name(" + name + ") replace", &rc);
            std::ifstream file(png, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();
            std::error_code ec;
            std::filesystem::remove(png, ec);
            if (rc != 0 || bytes.empty()) continue;

            adrastea::json data = { { "image/png", base64(bytes) }, { "text/plain", "<Stata graph: " + name + ">" } };
            displayData(data, adrastea::json::object(), adrastea::json::object());
        }
    }

    adrastea::json StataInterpreter::completeRequestImpl(const std::string& code, int cursor_pos)
    {
        text::CompletionToken token = text::completionToken(code, cursor_pos);

        std::string listing;
        switch (token.kind)
        {
        case text::CompletionKind::Variable:
            listing = runCaptured("mata: if (st_nvar()) printf(\"%s\\n\", invtokens(st_varname(1..st_nvar())))");
            break;
        case text::CompletionKind::Global:
            listing = runCaptured("mata: printf(\"%s\\n\", invtokens(st_dir(\"global\", \"macro\", \"*\")'))");
            break;
        case text::CompletionKind::Local:
            listing = runCaptured("mata: printf(\"%s\\n\", invtokens(st_dir(\"local\", \"macro\", \"*\")'))");
            break;
        case text::CompletionKind::None:
            return adrastea::createCompleteReply(adrastea::json::array(), cursor_pos, cursor_pos);
        }

        std::vector<std::string> matches = text::matchingNames(text::splitNames(listing), token.prefix);
        return adrastea::createCompleteReply(adrastea::json(matches), token.start, cursor_pos);
    }

    adrastea::json StataInterpreter::inspectRequestImpl(const std::string& code, int cursor_pos, int /*detail_level*/)
    {
        std::string token = text::tokenAt(code, cursor_pos);
        if (token.empty())
        {
            return adrastea::createInspectReply(false);
        }

        int rc = 0;
        runCaptured("confirm variable " + token + ", exact", &rc);
        if (rc != 0)
        {
            return adrastea::createInspectReply(false);
        }

        runCaptured("capture _return hold __callisto_r");
        std::string description = runCaptured("describe " + token) + runCaptured("summarize " + token);
        runCaptured("capture _return restore __callisto_r");

        adrastea::json data = { { "text/plain", text::trim(description) } };
        return adrastea::createInspectReply(true, data);
    }

    adrastea::json StataInterpreter::isCompleteRequestImpl(const std::string& code)
    {
        return adrastea::createIsCompleteReply(text::isComplete(code));
    }

    bool StataInterpreter::answersWhileBusyImpl(const std::string& msg_type) const
    {
        // is_complete never calls into Stata (stata_text.cpp), so it can be
        // answered while a cell runs. Completion and inspection need Stata,
        // which runs one command at a time.
        return msg_type == "is_complete_request";
    }

    adrastea::json StataInterpreter::shutdownRequestImpl(bool restart)
    {
        shutdownStata();
        return adrastea::createShutdownReply(restart);
    }

    adrastea::json StataInterpreter::interruptRequestImpl()
    {
        // From the control thread while the interpreter thread is inside
        // StataSO_Execute(); Stata stops at its next break check with r(1).
        // Only while a cell runs, so a stray interrupt does not break the next.
        if (m_executing.load())
        {
            StataSO_SetBreak();
        }
        return adrastea::createInterruptReply();
    }

    adrastea::json StataInterpreter::kernelInfoRequestImpl()
    {
        return adrastea::createInfoReply(
            "callisto",
            std::string(adrastea::version::kernel_protocol_version),
            "stata",
            m_version,
            "text/x-stata",
            ".do",
            "stata",
            std::string("stata"),
            "",
            "callisto (Stata " + m_version + " " + m_edition + ")",
            adrastea::json::array());
    }
}
