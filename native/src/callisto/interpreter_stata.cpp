#include "callisto/interpreter_stata.hpp"
#include "adrastea/guid.hpp"
#include "adrastea/helper.hpp"
#include "adrastea/input.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <regex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include <openssl/evp.h>

#include "callisto/plugin_sink.h"
#include "callisto/stata/stata_dynlib.hpp"
#include "callisto/stata/stata_mata.hpp"
#include "callisto/stata/stata_text.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// Stata embedded through its shared library (stata/stata_dynlib.hpp). Every
// request is answered by running Stata commands:
//
// - execute: a cell that is one plain command is run as typed at Stata's
//   prompt (StataSO_Execute, as pystata runs one line); any other cell is
//   written to a temporary do-file and run with `include`, not `do`, so it
//   runs in the interactive context -- local macros defined in one cell are
//   still there in the next -- and multi-line constructs (/* */, ///, loops,
//   programs, #delimit) work as in any do-file. A do-file's commands are
//   echoed; that echo is taken out of the output (text::EchoFilter), so a
//   cell shows what its commands print either way. Stata prints into its
//   output buffer; a second thread drains that buffer every few milliseconds
//   while the command runs and publishes it as stdout (pystata streams output
//   the same way), as valid UTF-8. A non-zero return code is an error whose
//   ename is Stata's "r(<rc>)" and whose evalue is the message printed above
//   it.
// - graphs: `_gr_list on` makes Stata record the graphs a cell draws;
//   afterwards each is exported to a PNG and published as display_data, as
//   pystata's inline graphs do.
// - what the kernel reads from Stata -- names, the dataset's description and
//   values -- comes from Callisto's Mata library (stata/stata_mata.hpp), whose
//   functions write JSON to a file the kernel reads: pystata's sfi, which
//   reads them without printing, is Python's only, and printed output is
//   wrapped at c(linesize). Values in bulk come through Callisto's plugin
//   (callisto_stata.plugin, Stata's plugin interface), which hands each to the
//   kernel directly. Mata and the plugin leave r() alone.
// - the dataset: the user_expressions `.callisto_dataset` (its description)
//   and `.callisto_data` (observations), which Jovian's Session asks with an
//   empty, silent cell (stataDataset(), stataData()).
// - complete / inspect: completed are variables and scalars, macros, stored
//   results (r(), e(), s()) and, for a command's first word, commands.
//   Inspecting a variable runs describe and summarize between `_return hold`
//   and `_return restore`, so the user's r() results survive; a scalar, a
//   global macro or a command shows its value or where it comes from.
// - interrupt: StataSO_SetBreak(), Stata's own Break.
namespace callisto
{
    using namespace callisto::stata;

    namespace
    {
        // How much of an execution's output is kept to find its error message.
        constexpr std::size_t kErrorTailBytes = 16 * 1024;
        constexpr auto kOutputPollInterval = std::chrono::milliseconds(20);
        // How long the output is quiet before the lines text::TailHold holds go out
        constexpr auto kHoldQuiet = std::chrono::milliseconds(150);

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

        // The folder of the running executable (callisto, or a test), where the plugin is
        std::filesystem::path executableDirectory()
        {
#ifdef _WIN32
            std::wstring path(32768, L'\0');
            DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            path.resize(length);
            return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
            char path[4096];
            uint32_t size = sizeof(path);
            if (_NSGetExecutablePath(path, &size) != 0) return {};
            return std::filesystem::weakly_canonical(path).parent_path();
#else
            std::error_code ec;
            return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#endif
        }

        // Plain Stata names only: they go into commands
        bool isName(const std::string& name)
        {
            if (name.empty() || name.size() > 32 || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
            return std::all_of(name.begin(), name.end(), [](char c) {
                unsigned char u = static_cast<unsigned char>(c);
                return u >= 0x80 || std::isalnum(u) || c == '_';
            });
        }

        std::string readFile(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        // Valid UTF-8, a Latin-1 byte read as Latin-1 (text::Utf8Decoder)
        std::string utf8(const std::string& bytes)
        {
            text::Utf8Decoder decoder;
            return decoder.push(bytes) + decoder.finish();
        }

        // The rows the plugin hands over (CallistoSink), as JSON
        struct RowCollector
        {
            adrastea::json rows = adrastea::json::array();
            adrastea::json row = adrastea::json::array();

            static void number(void* self, double value) { static_cast<RowCollector*>(self)->row.push_back(value); }
            static void missing(void* self, int code)
            {
                auto& row = static_cast<RowCollector*>(self)->row;
                if (code == 0) row.push_back(nullptr);
                else row.push_back(std::string(".") + static_cast<char>('a' + code - 1));
            }
            static void text(void* self, const char* bytes, size_t length)
            {
                static_cast<RowCollector*>(self)->row.push_back(utf8(std::string(bytes, length)));
            }
            static void rowEnd(void* self)
            {
                auto* collector = static_cast<RowCollector*>(self);
                collector->rows.push_back(std::move(collector->row));
                collector->row = adrastea::json::array();
            }
        };

        constexpr int kMataFunctionNotFound = 3499;
        // A page of observations at most, and value labels' values at most, sent at once
        constexpr long long kMaxRows = 100000;
        constexpr double kMaxLabelValues = 1000;
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
        text::Utf8Decoder decoder;
        std::string startup = text::trim(decoder.push(takeOutput()) + decoder.finish());
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
        std::string javaHome = text::trim(text::unwrap(runCaptured("display " + quoted("`c(java_home)'"))));
        if (!javaHome.empty())
        {
            const char* path = std::getenv("PATH");
            setEnv("PATH", std::string(path ? path : "") + ";" + javaHome + "\\bin");
        }
#endif

        installMataLibrary();
        const char* plugin = std::getenv("CALLISTO_PLUGIN");
        std::filesystem::path pluginPath = plugin && *plugin ? std::filesystem::path(plugin)
                                                             : executableDirectory() / "callisto_stata.plugin";
        std::error_code ec;
        if (std::filesystem::exists(pluginPath, ec)) m_pluginPath = pluginPath;

        adrastea::registerInterpreter(this);
    }

    StataInterpreter::~StataInterpreter()
    {
        shutdownStata();
        std::error_code ec;
        if (!m_libraryDir.empty()) std::filesystem::remove_all(m_libraryDir, ec);
    }

    bool StataInterpreter::installMataLibrary()
    {
        std::error_code ec;
        if (m_libraryDir.empty()) m_libraryDir = std::filesystem::temp_directory_path() / ("callisto-lib-" + adrastea::newGuid().toString());
        std::filesystem::create_directories(m_libraryDir, ec);
        std::string source = kMataLibrary;
        const std::string dir = stataPath(m_libraryDir);
        for (std::size_t at = source.find("@DIR@"); at != std::string::npos; at = source.find("@DIR@", at + dir.size()))
        {
            source.replace(at, 5, dir);
        }
        const std::filesystem::path doFile = m_libraryDir / "callisto.do";
        {
            std::ofstream file(doFile, std::ios::binary);
            file << source;
        }
        // browse and edit: the programs that ask for the host's data viewer
        for (const char* name : kBrowseNames)
        {
            std::string program = kBrowseProgram;
            program.replace(program.find("@NAME@"), 6, name);
            std::ofstream file(m_libraryDir / (std::string(name) + ".ado"), std::ios::binary);
            file << program;
        }
        int rc = 0;
        std::string output = runCaptured("quietly do " + quoted(stataPath(doFile)), &rc);
        if (rc != 0)
        {
            fprintf(stderr, "[callisto] the Mata library could not be compiled (r(%d)): %s\n", rc, text::trim(output).c_str());
            fflush(stderr);
            return false;
        }
        return true;
    }

    std::optional<adrastea::json> StataInterpreter::mataJson(const std::string& call)
    {
        const std::filesystem::path out = tempFile(".json");
        std::string command = call;
        const std::string target = "\"" + stataPath(out) + "\"";
        command.replace(command.find("@OUT@"), 5, target);
        int rc = 0;
        runCaptured("mata: " + command, &rc);
        if (rc == kMataFunctionNotFound && installMataLibrary())
        {
            runCaptured("mata: " + command, &rc);
        }
        std::error_code ec;
        std::optional<adrastea::json> answer;
        if (rc == 0)
        {
            adrastea::json parsed = adrastea::json::parse(utf8(readFile(out)), nullptr, false);
            if (!parsed.is_discarded()) answer = std::move(parsed);
        }
        std::filesystem::remove(out, ec);
        return answer;
    }

    void StataInterpreter::askToViewData()
    {
        std::optional<adrastea::json> view = mataJson("callisto_take_view(@OUT@)");
        if (!view || !view->value("requested", false))
        {
            return;
        }
        adrastea::json variables = view->contains("variables") ? (*view)["variables"] : adrastea::json::array();
        adrastea::json ui = { { "method", "viewData" }, { "params", { { "variables", variables } } } };
        // Jovian's client answers every question its supervisor's kernels ask (with nothing when nobody listens)
        static const bool supervised = std::getenv("JOVIAN_SUPERVISED") != nullptr;
        bool opened = false;
        try
        {
            std::string reply = adrastea::blockingInputRequest("", false, supervised, ui);
            adrastea::json answer = adrastea::json::parse(reply.empty() ? "null" : reply, nullptr, false);
            opened = answer.is_object() && answer.value("ok", false);
        }
        catch (const std::exception&)
        {
            // not under Jovian, and the execution allows no input: no one to ask
        }
        if (!opened)
        {
            publishStream("stdout", "(there is no data viewer here: list shows the data)\n");
        }
    }

    std::vector<std::string> StataInterpreter::mataNames(const std::string& kind)
    {
        std::optional<adrastea::json> names = mataJson("callisto_names(\"" + kind + "\", @OUT@)");
        std::vector<std::string> result;
        if (names && names->is_array())
        {
            for (const auto& name : *names)
            {
                if (name.is_string()) result.push_back(name.get<std::string>());
            }
        }
        return result;
    }

    adrastea::json StataInterpreter::describeDataset()
    {
        std::optional<adrastea::json> dataset = mataJson("callisto_dataset(@OUT@, " + std::to_string(static_cast<int>(kMaxLabelValues)) + ")");
        if (!dataset) throw std::runtime_error("the dataset could not be read");
        return *dataset;
    }

    std::optional<adrastea::json> StataInterpreter::pluginRows(const std::vector<std::string>& variables, long long first, long long last)
    {
        if (m_pluginPath.empty()) return std::nullopt;
        // defined again when it is gone (clear all, program drop _all); already defined is fine
        runCaptured("capture program _callisto_plugin, plugin using(" + quoted(stataPath(m_pluginPath)) + ")");
        RowCollector collector;
        CallistoSink sink{ CALLISTO_SINK_VERSION, &collector, &RowCollector::number, &RowCollector::missing,
            &RowCollector::text, &RowCollector::rowEnd };
        std::string varlist;
        for (const auto& name : variables) varlist += " " + name;
        int rc = 0;
        runCaptured("plugin call _callisto_plugin" + varlist + " in " + std::to_string(first) + "/" + std::to_string(last) + ", " +
                        std::to_string(reinterpret_cast<std::uintptr_t>(&sink)),
            &rc);
        if (rc != 0) return std::nullopt;
        return std::move(collector.rows);
    }

    adrastea::json StataInterpreter::readData(const adrastea::json& request)
    {
        const adrastea::json params = request.is_object() ? request : adrastea::json::object();
        std::vector<std::string> variables;
        if (params.contains("variables") && params["variables"].is_array())
        {
            for (const auto& name : params["variables"])
            {
                if (!name.is_string() || !isName(name.get<std::string>())) throw std::runtime_error("not a variable name: " + name.dump());
                variables.push_back(name.get<std::string>());
            }
        }
        if (variables.empty()) variables = mataNames("variables");
        const bool formatted = params.value("formatted", false);

        int rc = 0;
        const long long observations = std::stoll(text::trim(runCaptured("display _N", &rc)));
        const long long start = std::max<long long>(1, params.value("start", 1LL));
        const long long count = std::clamp<long long>(params.value("count", 100LL), 0, kMaxRows);
        const long long last = std::min(observations, start + count - 1);

        adrastea::json rows = adrastea::json::array();
        if (!variables.empty() && last >= start)
        {
            for (const auto& name : variables)
            {
                runCaptured("confirm variable " + name + ", exact", &rc);
                if (rc != 0) throw std::runtime_error("no variable " + name);
            }
            std::optional<adrastea::json> read;
            if (!formatted) read = pluginRows(variables, start, last);
            if (!read)
            {
                std::string names;
                for (const auto& name : variables) names += (names.empty() ? "" : " ") + name;
                read = mataJson("callisto_rows(@OUT@, \"" + names + "\", " + std::to_string(start) + ", " + std::to_string(last) + ", " +
                                (formatted ? "1" : "0") + ")");
            }
            if (!read) throw std::runtime_error("the data could not be read");
            rows = std::move(*read);
        }
        return { { "start", start }, { "count", rows.size() }, { "observations", observations }, { "variables", variables },
            { "formatted", formatted }, { "rows", std::move(rows) } };
    }

    adrastea::json StataInterpreter::evaluateUserExpressions(const adrastea::json& expressions)
    {
        adrastea::json results = adrastea::json::object();
        if (!expressions.is_object()) return results;
        for (auto it = expressions.begin(); it != expressions.end(); ++it)
        {
            const std::string expr = it.value().is_string() ? it.value().get<std::string>() : std::string();
            if (it.key() == ".callisto_dataset" || it.key() == ".callisto_data")
            {
                try
                {
                    adrastea::json answer = it.key() == ".callisto_dataset"
                                                ? describeDataset()
                                                : readData(adrastea::json::parse(expr.empty() ? "{}" : expr));
                    results[it.key()] = { { "status", "ok" }, { "data", { { "text/plain", answer.dump() } } },
                        { "metadata", adrastea::json::object() } };
                }
                catch (const std::exception& e)
                {
                    results[it.key()] = { { "status", "error" }, { "ename", "CallistoError" }, { "evalue", e.what() },
                        { "traceback", adrastea::json::array() } };
                }
                continue;
            }
            int exprRc = 0;
            std::string output = runCaptured("display " + expr, &exprRc);
            if (exprRc == 0)
            {
                results[it.key()] = { { "status", "ok" },
                    { "data", { { "text/plain", text::trim(output) } } }, { "metadata", adrastea::json::object() } };
            }
            else
            {
                results[it.key()] = { { "status", "error" },
                    { "ename", "r(" + std::to_string(exprRc) + ")" },
                    { "evalue", text::errorMessage(output, exprRc) },
                    { "traceback", adrastea::json::array() } };
            }
        }
        return results;
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
        text::Utf8Decoder decoder;
        return decoder.push(takeOutput()) + decoder.finish();
    }

    std::vector<std::string> StataInterpreter::commandNames()
    {
        if (!m_adoCommandsRead)
        {
            m_adoCommandsRead = true;
            m_adoCommands.clear();
            std::set<std::string> seen;
            // BASE;SITE;.;PERSONAL;PLUS;OLDPLACE, as directories; each holds its
            // ado-files in itself and in one-letter folders (base/r/regress.ado)
            for (const std::string& directory : mataNames("adopath"))
            {
                std::error_code ec;
                std::vector<std::filesystem::path> folders = { std::filesystem::path(directory) };
                for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
                {
                    std::string name = entry.path().filename().string();
                    if (entry.is_directory(ec) && name.size() == 1) folders.push_back(entry.path());
                }
                for (const auto& folder : folders)
                {
                    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
                    {
                        if (entry.path().extension() != ".ado") continue;
                        std::string name = entry.path().stem().string();
                        // _name.ado are the internals of other commands
                        if (!name.empty() && name[0] != '_' && seen.insert(name).second) m_adoCommands.push_back(name);
                    }
                }
            }
            // regress_estat.ado, table_parse_cmd.ado: parts of another command, not commands
            const std::vector<std::string>& builtin = text::builtinCommands();
            seen.insert(builtin.begin(), builtin.end());
            std::erase_if(m_adoCommands, [&](const std::string& name) {
                for (std::size_t underscore = name.find('_'); underscore != std::string::npos; underscore = name.find('_', underscore + 1))
                {
                    if (seen.count(name.substr(0, underscore))) return true;
                }
                return false;
            });
        }

        std::vector<std::string> names = text::builtinCommands();
        names.insert(names.end(), m_adoCommands.begin(), m_adoCommands.end());
        // programs defined in this session: `program dir` lists them, the name last on each line
        for (const std::string& line : text::printedLines(runCaptured("program dir")))
        {
            std::string name = line.substr(line.find_last_of(' ') == std::string::npos ? 0 : line.find_last_of(' ') + 1);
            if (!name.empty() && !std::isdigit(static_cast<unsigned char>(name[0])) && name[0] != '_') names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
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
        // nothing to run: only the user expressions (Jovian's stataDataset() / stataData() send these)
        if (text::trim(code).empty())
        {
            cb(adrastea::createSuccessfulReply(adrastea::json::array(), evaluateUserExpressions(user_expressions)));
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

        // One plain command runs as typed at the prompt; anything else as a do-file, whose echo is filtered out
        const bool asTyped = text::isSingleCommand(code);
        text::Utf8Decoder decoder;
        text::EchoFilter echo;
        // the last lines wait: a failing cell's error message is the error the kernel publishes, not output too
        text::TailHold hold;
        std::string tail;
        auto lastOutput = std::chrono::steady_clock::now();
        auto show = [&](const std::string& text) {
            if (!config.silent && !text.empty()) publishStream("stdout", text);
        };
        auto publish = [&](const std::string& chunk, bool last) {
            std::string decoded = decoder.push(chunk);
            if (last) decoded += decoder.finish();
            if (!decoded.empty()) lastOutput = std::chrono::steady_clock::now();
            tail += decoded;
            if (tail.size() > kErrorTailBytes) tail.erase(0, tail.size() - kErrorTailBytes);
            std::string shown = asTyped ? decoded : echo.push(decoded);
            if (last && !asTyped) shown += echo.finish();
            show(hold.push(shown));
            // quiet for a moment: what is held goes out too (a long command's last lines need not wait for its end)
            if (!last && std::chrono::steady_clock::now() - lastOutput >= kHoldQuiet) show(hold.release());
        };

        StataSO_ClearOutputBuffer();
        std::mutex pollMutex;
        std::condition_variable pollWake;
        bool finished = false;
        std::thread poller([&]() {
            std::unique_lock<std::mutex> lock(pollMutex);
            while (!finished)
            {
                lock.unlock();
                publish(takeOutput(), false);
                lock.lock();
                // woken at once when the command returns, so the cell's end waits for no poll
                pollWake.wait_for(lock, kOutputPollInterval, [&] { return finished; });
            }
        });

        int rc = 0;
        {
            m_executing = true;
            const std::string command = asTyped ? text::trim(code) : "include " + quoted(stataPath(doFile));
            rc = StataSO_Execute(command.c_str(), 0);
            m_executing = false;
        }
        {
            std::lock_guard<std::mutex> lock(pollMutex);
            finished = true;
        }
        pollWake.notify_one();
        poller.join();
        publish(takeOutput(), true);

        std::string ename;
        std::string evalue;
        std::string printedError; // the message and "r(<rc>);" as Stata printed them, shown as the error instead
        if (rc != 0)
        {
            ename = "r(" + std::to_string(rc) + ")";
            evalue = text::errorMessage(tail, rc);
            if (!evalue.empty()) printedError = evalue + "\n" + ename + ";";
            else evalue = rc == 1 ? "--Break--" : "Stata returned an error";
        }
        show(hold.finish(printedError));

        std::error_code ec;
        std::filesystem::remove(doFile, ec);

        // A cell that may have installed commands or changed where they are found
        if (code.find("install") != std::string::npos || code.find("adopath") != std::string::npos ||
            code.find("sysdir") != std::string::npos || code.find("net ") != std::string::npos)
        {
            m_adoCommandsRead = false;
        }

        if (showGraphs)
        {
            publishGraphs();
            runCaptured("quietly _gr_list off");
        }

        // browse, edit: the host's data viewer (only a cell that may have browsed asks Mata whether it did)
        static const std::regex browsed(R"((^|[\s;:])(br|bro|brow|brows|browse|ed|edi|edit)\b)");
        if (!config.silent && std::regex_search(code, browsed))
        {
            askToViewData();
        }

        if (rc != 0)
        {
            publishExecutionError(ename, evalue, errorTraceback(ename, evalue));
            cb(adrastea::createErrorReply(ename, evalue, errorTraceback(ename, evalue)));
            return;
        }

        cb(adrastea::createSuccessfulReply(adrastea::json::array(), evaluateUserExpressions(user_expressions)));
    }

    void StataInterpreter::publishGraphs()
    {
        // `_gr_list list` returns its list in r(); keep the user's r() results.
        runCaptured("capture _return hold __callisto_r");
        runCaptured("quietly _gr_list list");
        std::vector<std::string> names = mataNames("graphs");
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

        std::vector<std::string> names;
        switch (token.kind)
        {
        case text::CompletionKind::Variable:
        {
            names = mataNames("variables");
            std::vector<std::string> scalars = mataNames("scalars");
            names.insert(names.end(), scalars.begin(), scalars.end());
            break;
        }
        case text::CompletionKind::Global:
            names = mataNames("globals");
            break;
        case text::CompletionKind::Local:
            names = mataNames("locals");
            break;
        case text::CompletionKind::Result:
            names = mataNames(std::string(1, token.resultClass) + "()");
            break;
        case text::CompletionKind::Command:
            names = commandNames();
            break;
        case text::CompletionKind::None:
            return adrastea::createCompleteReply(adrastea::json::array(), cursor_pos, cursor_pos);
        }

        std::vector<std::string> matches = text::matchingNames(names, token.prefix);
        return adrastea::createCompleteReply(adrastea::json(matches), token.start, cursor_pos);
    }

    adrastea::json StataInterpreter::inspectRequestImpl(const std::string& code, int cursor_pos, int /*detail_level*/)
    {
        std::string token = text::tokenAt(code, cursor_pos);
        if (token.empty())
        {
            return adrastea::createInspectReply(false);
        }

        auto found = [](const std::string& text) {
            return adrastea::createInspectReply(true, adrastea::json{ { "text/plain", text::trim(text) } });
        };

        // $name: a global macro's value
        std::size_t start = static_cast<std::size_t>(std::max(0, cursor_pos));
        start = std::min(start, code.size());
        while (start > 0 && code.compare(start - 1, 1, "$") != 0 && (std::isalnum(static_cast<unsigned char>(code[start - 1])) ||
                                                                     code[start - 1] == '_'))
        {
            --start;
        }
        int rc = 0;
        if (start > 0 && code[start - 1] == '$')
        {
            std::string listed = runCaptured("macro list " + token, &rc);
            return rc == 0 ? found(listed) : adrastea::createInspectReply(false);
        }

        runCaptured("confirm variable " + token + ", exact", &rc);
        if (rc == 0)
        {
            runCaptured("capture _return hold __callisto_r");
            std::string description = runCaptured("describe " + token) + runCaptured("summarize " + token);
            runCaptured("capture _return restore __callisto_r");
            return found(description);
        }

        runCaptured("confirm scalar " + token, &rc);
        if (rc == 0)
        {
            return found(runCaptured("scalar list " + token));
        }

        // a command: which says where it comes from (an ado-file and its version line, or built in)
        std::string which = runCaptured("which " + token, &rc);
        if (rc == 0)
        {
            return found(which);
        }
        return adrastea::createInspectReply(false);
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
