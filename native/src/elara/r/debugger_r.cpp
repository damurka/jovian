// The R kernel's debugger: the Jupyter debug protocol (JEP 47) on R's own browser(), as Ark's debugger is.
//
// A client (JupyterLab, VS Code) sends Debug Adapter Protocol requests in debug_request messages -- on the control
// channel, so also while a cell runs -- and gets DAP events (stopped, continued) in debug_event ones. The flow:
//
// - initialize, attach: the debugger is started. debugInfo tells the client how cells are named as files
//   (tmpFilePrefix + murmur2(code, hashSeed) + tmpFileSuffix); dumpCell writes a cell there.
// - setBreakpoints on such a file: a breakpoint in a function defined in the cell becomes R's own (utils::
//   setBreakpoint(), a trace() calling browser()), set again after every cell; one on a line of the cell's own code
//   puts a browser() call before that top-level expression (hera's .jv.debug.instrument()).
// - When R stops at a browser() prompt, RInterpreter::readConsole() hands over to stopped() here: the client is told
//   ("stopped"), its stackTrace, scopes, variables and evaluate requests are answered on R's thread -- in the
//   browser's context, so the call stack is the debugged code's -- until it continues (c), steps (n, s) or steps out
//   (f): that command goes to R's browser as its input.
// - pause interrupts R; hera's interrupt handler then calls browser() (.jv.debug.on_interrupt()).

#include "elara/r/debugger_r.hpp"

#include "elara/r/r_dynlib.hpp"
#include "elara/r/rtools.hpp"
#include "elara/r/json_convert.hpp"
#include "elara/interpreter_r.hpp"
#include "elara/log.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#define ELARA_GETPID _getpid
#else
#include <unistd.h>
#define ELARA_GETPID getpid
#endif

namespace elara
{
    namespace
    {
        // MurmurHash2 (32 bits), as Jupyter clients hash a cell's code for its file name (ipykernel's murmur2_x86)
        unsigned int murmur2(const std::string& data, unsigned int seed)
        {
            const unsigned int m = 0x5bd1e995;
            const int r = 24;
            size_t len = data.size();
            unsigned int h = seed ^ static_cast<unsigned int>(len);
            const unsigned char* p = reinterpret_cast<const unsigned char*>(data.data());
            while (len >= 4)
            {
                unsigned int k = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<unsigned int>(p[3]) << 24);
                k *= m;
                k ^= k >> r;
                k *= m;
                h *= m;
                h ^= k;
                p += 4;
                len -= 4;
            }
            switch (len)
            {
            case 3: h ^= p[2] << 16; [[fallthrough]];
            case 2: h ^= p[1] << 8; [[fallthrough]];
            case 1: h ^= p[0]; h *= m;
            }
            h ^= h >> 13;
            h *= m;
            h ^= h >> 15;
            return h;
        }

        SEXP heraFunction(const char* name)
        {
            return r::elaraFunction(name);
        }

        // hera's `name`(args...): with Rf_eval when `inContext` (the call stack must be R's own: the function
        // catches its errors itself), else R_tryEval. NULL on an error.
        SEXP callHera(const char* name, SEXP args, bool inContext)
        {
            SEXP fn = PROTECT(heraFunction(name));
            if (fn == R_NilValue)
            {
                UNPROTECT(1);
                return R_NilValue;
            }
            SEXP call = PROTECT(Rf_lcons(fn, args));
            SEXP value = R_NilValue;
            if (inContext)
            {
                value = Rf_eval(call, R_GlobalEnv);
            }
            else
            {
                int error = 0;
                value = R_tryEval(call, R_GlobalEnv, &error);
                if (error) value = R_NilValue;
            }
            UNPROTECT(2);
            return value;
        }

        SEXP listElement(SEXP list, const char* name)
        {
            SEXP names = Rf_getAttrib(list, Rf_install("names"));
            if (TYPEOF(list) != VECSXP || TYPEOF(names) != STRSXP) return R_NilValue;
            for (R_xlen_t i = 0; i < XLENGTH(list); ++i)
            {
                if (std::strcmp(CHAR(STRING_ELT(names, i)), name) == 0) return VECTOR_ELT(list, i);
            }
            return R_NilValue;
        }

        SEXP mkStringUtf8(const std::string& s)
        {
            SEXP out = PROTECT(Rf_allocVector(STRSXP, 1));
            SET_STRING_ELT(out, 0, Rf_mkCharLenCE(s.data(), static_cast<int>(s.size()), CE_UTF8));
            UNPROTECT(1);
            return out;
        }
    }

    RDebugger::RDebugger(RInterpreter& interpreter)
        : m_interpreter(interpreter)
    {
        std::error_code ec;
        auto dir = std::filesystem::temp_directory_path(ec) / ("elara_debug_" + std::to_string(ELARA_GETPID()));
        std::filesystem::create_directories(dir, ec);
        m_tmpPrefix = dir.generic_string() + "/";
    }

    std::string RDebugger::cellPath(const std::string& code) const
    {
        return m_tmpPrefix + std::to_string(murmur2(code, m_hashSeed)) + ".r";
    }

    std::vector<int> RDebugger::breakpointLines(const std::string& path) const
    {
        auto it = m_breakpoints.find(path);
        return it == m_breakpoints.end() ? std::vector<int>() : it->second;
    }

    adrastea::json RDebugger::response(const adrastea::json& request, bool success, adrastea::json body, const std::string& message) const
    {
        adrastea::json out = {
            {"type", "response"},
            {"seq", 0},
            {"request_seq", request.value("seq", 0)},
            {"success", success},
            {"command", request.value("command", "")},
            {"body", std::move(body)}};
        if (!message.empty()) out["message"] = message;
        return out;
    }

    void RDebugger::event(const std::string& name, adrastea::json body, bool onRThread)
    {
        adrastea::json e = {{"type", "event"}, {"seq", ++m_seq}, {"event", name}, {"body", std::move(body)}};
        // (the output buffers are the R thread's: not flushed from another)
        m_interpreter.publishDebugEvent(std::move(e), onRThread);
    }

    // Work that needs R: done at once on R's thread; from another thread, by R's thread when it is stopped at a
    // breakpoint (stopped() runs it), and otherwise not at all (R is busy running the cell)
    adrastea::json RDebugger::onRThread(const adrastea::json& request, const std::function<adrastea::json()>& work)
    {
        if (std::this_thread::get_id() == m_rThread)
        {
            return work();
        }
        auto done = std::make_shared<std::promise<adrastea::json>>();
        auto result = done->get_future();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_stopped)
            {
                return response(request, false, adrastea::json::object(), "R is running: only answered when it is stopped");
            }
            m_jobs.emplace_back(work, done);
        }
        m_wake.notify_all();
        if (result.wait_for(std::chrono::seconds(60)) != std::future_status::ready)
        {
            // not run after all, if R's thread has not taken it yet; if it has, its answer goes to a promise nobody
            // waits on, which is fine
            std::lock_guard<std::mutex> lock(m_mutex);
            std::erase_if(m_jobs, [&](const Job& job) { return job.second == done; });
            return response(request, false, adrastea::json::object(), "R did not answer");
        }
        return result.get();
    }

    adrastea::json RDebugger::request(const adrastea::json& req)
    {
        const std::string command = req.value("command", "");
        const adrastea::json args = req.value("arguments", adrastea::json::object());

        if (command == "initialize")
        {
            return response(req, true, {
                {"supportsConfigurationDoneRequest", true},
                {"supportsEvaluateForHovers", true},
                {"supportsConditionalBreakpoints", false},
                {"supportsStepBack", false},
                {"supportsSetVariable", false},
                {"supportsTerminateRequest", false},
                {"exceptionBreakpointFilters", adrastea::json::array()}});
        }
        if (command == "attach" || command == "launch")
        {
            m_started = true;
            auto reply = response(req, true);
            event("initialized", adrastea::json::object(), std::this_thread::get_id() == m_rThread);
            return reply;
        }
        if (command == "disconnect" || command == "terminate")
        {
            m_started = false;
            m_breakpoints.clear();
            m_breakpointsChanged = true;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopped) m_resume = "c";
            }
            m_wake.notify_all();
            if (std::this_thread::get_id() == m_rThread) applyBreakpoints();
            return response(req, true);
        }
        if (command == "configurationDone")
        {
            return response(req, true);
        }
        if (command == "debugInfo")
        {
            adrastea::json breakpoints = adrastea::json::array();
            for (const auto& [path, lines] : m_breakpoints)
            {
                adrastea::json list = adrastea::json::array();
                for (int line : lines) list.push_back({{"line", line}});
                breakpoints.push_back({{"source", path}, {"breakpoints", list}});
            }
            bool stopped;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                stopped = m_stopped;
            }
            return response(req, true, {
                {"isStarted", m_started},
                {"hashMethod", "Murmur2"},
                {"hashSeed", m_hashSeed},
                {"tmpFilePrefix", m_tmpPrefix},
                {"tmpFileSuffix", ".r"},
                {"breakpoints", breakpoints},
                {"stoppedThreads", stopped ? adrastea::json::array({1}) : adrastea::json::array()},
                {"richRendering", false},
                {"exceptionPaths", adrastea::json::array()}});
        }
        if (command == "dumpCell")
        {
            std::string code = args.value("code", "");
            std::string path = cellPath(code);
            std::ofstream out(std::filesystem::u8path(path), std::ios::binary);
            out << code;
            return response(req, true, {{"sourcePath", path}});
        }
        if (command == "setBreakpoints")
        {
            std::string path = args.value("source", adrastea::json::object()).value("path", "");
            std::vector<int> lines;
            adrastea::json verified = adrastea::json::array();
            for (const auto& b : args.value("breakpoints", adrastea::json::array()))
            {
                int line = b.value("line", 0);
                lines.push_back(line);
                verified.push_back({{"verified", true}, {"line", line}, {"source", {{"path", path}}}});
            }
            if (lines.empty()) m_breakpoints.erase(path);
            else m_breakpoints[path] = lines;
            m_breakpointsChanged = true;
            // functions already defined get them now when R is free (else after the cell under way)
            if (std::this_thread::get_id() == m_rThread) applyBreakpoints();
            return response(req, true, {{"breakpoints", verified}});
        }
        if (command == "threads")
        {
            return response(req, true, {{"threads", adrastea::json::array({{{"id", 1}, {"name", "R"}}})}});
        }
        if (command == "continue" || command == "next" || command == "stepIn" || command == "stepOut")
        {
            std::string input = command == "continue" ? "c" : command == "next" ? "n" : command == "stepIn" ? "s" : "f";
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_stopped) return response(req, false, adrastea::json::object(), "not stopped");
                m_resume = input;
            }
            m_wake.notify_all();
            return response(req, true, command == "continue" ? adrastea::json{{"allThreadsContinued", true}} : adrastea::json::object());
        }
        if (command == "pause")
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopped) return response(req, true);
                m_pauseRequested = true;
            }
            r::requestRInterrupt();
            return response(req, true);
        }
        if (command == "stackTrace")
        {
            return onRThread(req, [this, req]() { return stackTrace(req); });
        }
        if (command == "scopes")
        {
            int frame = args.value("frameId", 0);
            return response(req, true, {{"scopes", adrastea::json::array({
                {{"name", "Locals"}, {"variablesReference", frame}, {"expensive", false}},
                {{"name", "Globals"}, {"variablesReference", 0x7fffffff}, {"expensive", false}}})}});
        }
        if (command == "variables")
        {
            return onRThread(req, [this, req]() { return variables(req); });
        }
        if (command == "evaluate")
        {
            return onRThread(req, [this, req]() { return evaluate(req); });
        }
        if (command == "inspectVariables")
        {
            return onRThread(req, [this, req]() { return inspectVariables(req); });
        }
        if (command == "source")
        {
            std::string path = args.value("source", adrastea::json::object()).value("path", "");
            std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            if (!in) return response(req, false, adrastea::json::object(), "no such source: " + path);
            return response(req, true, {{"content", text.str()}});
        }
        return response(req, false, adrastea::json::object(), "not supported: " + command);
    }

    // ---- what needs R: on R's thread ---------------------------------------------------------------------------

    int RDebugger::reference(void* object)
    {
        SEXP value = static_cast<SEXP>(object);
        R_PreserveObject(value);
        int id = m_nextReference++;
        m_references[id] = value;
        return id;
    }

    void* RDebugger::referenced(int id) const
    {
        if (id == 0x7fffffff) return R_GlobalEnv;
        auto it = m_references.find(id);
        return it == m_references.end() ? nullptr : it->second;
    }

    void RDebugger::clearReferences()
    {
        for (auto& [id, value] : m_references) R_ReleaseObject(static_cast<SEXP>(value));
        m_references.clear();
        m_frames.clear();
    }

    adrastea::json RDebugger::stackTrace(const adrastea::json& req)
    {
        if (!m_frames.empty()) return response(req, true, {{"stackFrames", m_frames}, {"totalFrames", m_frames.size()}});
        // the debugged code's calls, innermost first, where each is: hera's .jv.debug.stack() (evaluated here in the
        // browser's context; it catches its own errors), the current statement being R_Srcref
        SEXP path = PROTECT(mkStringUtf8(m_locationPath));
        SEXP line = PROTECT(Rf_ScalarInteger(m_locationLine));
        SEXP args = PROTECT(Rf_cons(path, Rf_cons(line, R_NilValue)));
        SEXP stack = PROTECT(callHera(".jv.debug.stack", args, true));
        SEXP frames = listElement(stack, "frames");
        SEXP envs = listElement(stack, "envs");
        adrastea::json described = routines::sexpToJson(frames, false);
        adrastea::json out = adrastea::json::array();
        if (described.is_array() && TYPEOF(envs) == VECSXP)
        {
            for (size_t i = 0; i < described.size() && static_cast<R_xlen_t>(i) < XLENGTH(envs); ++i)
            {
                const auto& f = described[i];
                adrastea::json frame = {
                    {"id", reference(VECTOR_ELT(envs, static_cast<R_xlen_t>(i)))},
                    {"name", f.value("name", "")},
                    {"line", f.value("line", 0)},
                    {"column", f.value("column", 1)}};
                std::string path = f.value("path", "");
                if (!path.empty()) frame["source"] = {{"path", path}, {"name", std::filesystem::u8path(path).filename().u8string()}};
                out.push_back(frame);
            }
        }
        UNPROTECT(4);
        m_frames = out.get<std::vector<adrastea::json>>();
        return response(req, true, {{"stackFrames", out}, {"totalFrames", out.size()}});
    }

    // An object's or environment's variables (hera's .jv.debug.variables()): rows, and the children that can be
    // opened, which get a reference
    static adrastea::json variableRows(RDebugger& debugger, SEXP described, const std::function<int(SEXP)>& refer)
    {
        SEXP rows = listElement(described, "rows");
        SEXP children = listElement(described, "children");
        adrastea::json out = adrastea::json::array();
        adrastea::json list = routines::sexpToJson(rows, false);
        if (!list.is_array()) return out;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const auto& row = list[i];
            SEXP child = TYPEOF(children) == VECSXP && static_cast<R_xlen_t>(i) < XLENGTH(children) ? VECTOR_ELT(children, static_cast<R_xlen_t>(i)) : R_NilValue;
            out.push_back({
                {"name", row.value("name", "")},
                {"value", row.value("value", "")},
                {"type", row.value("type", "")},
                {"variablesReference", child == R_NilValue ? 0 : refer(child)}});
        }
        (void)debugger;
        return out;
    }

    adrastea::json RDebugger::variables(const adrastea::json& req)
    {
        int id = req.value("arguments", adrastea::json::object()).value("variablesReference", 0);
        SEXP object = static_cast<SEXP>(referenced(id));
        if (!object) return response(req, false, adrastea::json::object(), "no such variables reference");
        SEXP args = PROTECT(Rf_cons(object, R_NilValue));
        SEXP described = PROTECT(callHera(".jv.debug.variables", args, false));
        auto rows = variableRows(*this, described, [this](SEXP child) { return reference(child); });
        UNPROTECT(2);
        return response(req, true, {{"variables", rows}});
    }

    adrastea::json RDebugger::inspectVariables(const adrastea::json& req)
    {
        SEXP args = PROTECT(Rf_cons(R_GlobalEnv, R_NilValue));
        SEXP described = PROTECT(callHera(".jv.debug.variables", args, false));
        auto rows = variableRows(*this, described, [this](SEXP child) { return reference(child); });
        UNPROTECT(2);
        return response(req, true, {{"variables", rows}});
    }

    adrastea::json RDebugger::evaluate(const adrastea::json& req)
    {
        const auto args = req.value("arguments", adrastea::json::object());
        int frame = args.value("frameId", 0);
        SEXP env = static_cast<SEXP>(frame ? referenced(frame) : nullptr);
        if (!env) env = R_GlobalEnv;
        SEXP callArgs = PROTECT(Rf_cons(mkStringUtf8(args.value("expression", "")), Rf_cons(env, R_NilValue)));
        SEXP result = PROTECT(callHera(".jv.debug.evaluate", callArgs, false));
        adrastea::json described = routines::sexpToJson(listElement(result, "result"), false);
        SEXP child = listElement(result, "child");
        adrastea::json body = {
            {"result", described.is_string() ? described.get<std::string>() : described.dump()},
            {"variablesReference", child == R_NilValue ? 0 : reference(child)}};
        UNPROTECT(2);
        return response(req, true, body);
    }

    void RDebugger::applyBreakpoints()
    {
        if (!m_breakpointsChanged && m_breakpoints.empty()) return;
        m_breakpointsChanged = false;
        adrastea::json all = adrastea::json::object();
        for (const auto& [path, lines] : m_breakpoints) all[path] = lines;
        SEXP spec = PROTECT(routines::jsonToSexp(all.empty() ? adrastea::json::object() : all));
        SEXP args = PROTECT(Rf_cons(spec, R_NilValue));
        callHera(".jv.debug.apply_breakpoints", args, false);
        UNPROTECT(2);
    }

    bool RDebugger::takePauseRequest()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool requested = m_pauseRequested;
        m_pauseRequested = false;
        if (requested) m_pausing = true;
        return requested;
    }

    bool RDebugger::consoleLine(const std::string& line)
    {
        // a breakpoint's trace() prints where it is first, "<file name>#<line>" (the file is one of the cells')
        auto hash = line.rfind('#');
        if (hash != std::string::npos && hash > 0 && hash + 1 < line.size() && line.find(' ') == std::string::npos &&
            line.find_first_not_of("0123456789", hash + 1) == std::string::npos)
        {
            std::string file = line.substr(0, hash);
            m_locationPath = file.find('/') == std::string::npos && file.find('\\') == std::string::npos ? m_tmpPrefix + file : file;
            m_locationLine = std::atoi(line.c_str() + hash + 1);
            return true;
        }
        if (line.rfind("Called from: ", 0) == 0)
        {
            m_calledFrom = true;
            return true;
        }
        if (line.rfind("debug at ", 0) == 0)
        {
            // "debug at <path>#<line>: <expression>"
            auto hash = line.find('#', 9);
            auto colon = hash == std::string::npos ? std::string::npos : line.find(':', hash);
            if (colon != std::string::npos)
            {
                m_locationPath = line.substr(9, hash - 9);
                m_locationLine = std::atoi(line.substr(hash + 1, colon - hash - 1).c_str());
            }
            return true;
        }
        return line.rfind("debug: ", 0) == 0 || line.rfind("Browse[", 0) == 0;
    }

    bool RDebugger::takeBrowsed()
    {
        bool browsed = m_browsed;
        m_browsed = false;
        return browsed;
    }

    int RDebugger::stopped(unsigned char* buffer, int length)
    {
        m_browsed = true;
        auto answer = [&](const std::string& command) {
            std::string line = command + "\n";
            if (static_cast<int>(line.size()) + 1 > length) return 0;
            std::memcpy(buffer, line.c_str(), line.size() + 1);
            return 1;
        };
        std::string reason;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_calledFrom && !m_pausing)
            {
                // stopped at the browser() call of a breakpoint: one step on, to the statement itself (as Ark
                // does), unseen by the client
                m_calledFrom = false;
                m_lastResume = "c";
                return answer("n");
            }
            m_calledFrom = false;
            m_stopped = true;
            m_resume.clear();
            reason = m_pausing ? "pause" : m_lastResume == "c" ? "breakpoint" : "step";
            m_pausing = false;
        }
        event("stopped", {{"reason", reason}, {"threadId", 1}, {"allThreadsStopped", true}}, true);

        std::string resume;
        for (;;)
        {
            Job job;
            bool haveJob = false;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait_for(lock, std::chrono::milliseconds(50), [this] { return !m_jobs.empty() || !m_resume.empty(); });
                if (!m_jobs.empty())
                {
                    job = std::move(m_jobs.front());
                    m_jobs.pop_front();
                    haveJob = true;
                }
                else if (!m_resume.empty())
                {
                    resume = m_resume;
                    m_resume.clear();
                    m_stopped = false;
                    break;
                }
            }
            if (haveJob)
            {
                job.second->set_value(job.first());
                continue;
            }
            if (m_interpreter.interruptRequested())
            {
                // interrupted while stopped: out of the browser, and so of the cell
                std::lock_guard<std::mutex> lock(m_mutex);
                resume = "Q";
                m_stopped = false;
                break;
            }
            if (!m_started)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                resume = "c";
                m_stopped = false;
                break;
            }
        }
        // requests still waiting (a disconnect raced them): answered as unanswerable
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& [work, promise] : m_jobs)
            {
                promise->set_value(adrastea::json{{"type", "response"}, {"success", false}, {"message", "R is running"}});
            }
            m_jobs.clear();
            m_lastResume = resume;
        }
        clearReferences();
        event("continued", {{"threadId", 1}, {"allThreadsContinued", true}}, true);
        return answer(resume);
    }
}
