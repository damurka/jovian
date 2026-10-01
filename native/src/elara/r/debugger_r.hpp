#ifndef ELARA_R_DEBUGGER_R_HPP
#define ELARA_R_DEBUGGER_R_HPP

// The R kernel's debugger, for the Jupyter debug protocol (JEP 47: Debug Adapter Protocol requests in debug_request
// messages, events in debug_event ones) -- see debugger_r.cpp.

#include "adrastea/json.hpp"

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace elara
{
    class RInterpreter;

    class RDebugger
    {
    public:
        explicit RDebugger(RInterpreter& interpreter);

        // R's thread: the one running R's console loop (requests on it are answered at once)
        void setRThread(std::thread::id id) { m_rThread = id; }

        // A DAP request (a debug_request's content) and its response. On the R thread (the kernel idle), or on the
        // control watcher thread while a cell runs: then what needs R waits for R to stop at a breakpoint, or is
        // answered without it.
        adrastea::json request(const adrastea::json& request);

        bool started() const { return m_started; }

        // The file a cell's code is run from while debugging, named as Jupyter clients name it (debugInfo's
        // tmpFilePrefix + murmur2(code) + tmpFileSuffix): breakpoints set on it apply to the cell.
        std::string cellPath(const std::string& code) const;

        // The breakpoint lines in a file
        std::vector<int> breakpointLines(const std::string& path) const;

        // R stopped at a browser() prompt (a breakpoint, a step) while debugging: tells the client, answers its
        // requests until it continues or steps, and returns that command for R (into `buffer`, as ReadConsole does)
        int stopped(unsigned char* buffer, int length);

        // Breakpoints in functions (R's trace()), set again: after a cell, which may have defined them anew
        void applyBreakpoints();

        // An interrupt while debugging: a requested pause (the client's "pause") stops in browser()
        bool takePauseRequest();

        // A line R's console printed while debugging: browser()'s own ("Called from: f(10)", "debug at
        // file#2: y <- x + 1") tell where R stopped, and are not shown -- true for those
        bool consoleLine(const std::string& line);

        // Whether a browser() prompt was answered during the expression being evaluated (R's console then needs a
        // turn to reset its input: see RInterpreter::evaluateNext())
        bool takeBrowsed();

    private:
        adrastea::json response(const adrastea::json& request, bool success, adrastea::json body = adrastea::json::object(),
                                const std::string& message = "") const;
        void event(const std::string& name, adrastea::json body, bool onRThread);
        adrastea::json onRThread(const adrastea::json& request, const std::function<adrastea::json()>& work);
        adrastea::json stackTrace(const adrastea::json& request);
        adrastea::json variables(const adrastea::json& request);
        adrastea::json evaluate(const adrastea::json& request);
        adrastea::json inspectVariables(const adrastea::json& request);
        int reference(void* object);
        void* referenced(int reference) const;
        void clearReferences();

        RInterpreter& m_interpreter;
        std::thread::id m_rThread;
        bool m_started = false;
        int m_seq = 0;
        unsigned int m_hashSeed = 3339675911u;
        std::string m_tmpPrefix;
        std::map<std::string, std::vector<int>> m_breakpoints;
        bool m_breakpointsChanged = false;

        // stopped at a browser() prompt: requests for R are run there (see stopped())
        std::mutex m_mutex;
        std::condition_variable m_wake;
        bool m_stopped = false;
        std::string m_resume;
        std::string m_lastResume = "c";
        bool m_pauseRequested = false;
        bool m_pausing = false;      // the pause asked for is being taken (hera's interrupt handler calls browser())
        bool m_calledFrom = false;   // R stopped at a browser() call itself ("Called from:"), not at a step
        bool m_browsed = false;
        std::string m_locationPath;  // where R is, from its last "debug at file#line:"
        int m_locationLine = 0;
        std::deque<std::pair<std::function<adrastea::json()>, std::promise<adrastea::json>*>> m_jobs;

        // variablesReference -> the frame environment or object (preserved), while stopped
        std::map<int, void*> m_references;
        int m_nextReference = 1;
        std::vector<adrastea::json> m_frames;
    };
}

#endif // ELARA_R_DEBUGGER_R_HPP
