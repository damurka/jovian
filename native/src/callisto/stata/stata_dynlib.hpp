#ifndef CALLISTO_STATA_DYNLIB_HPP
#define CALLISTO_STATA_DYNLIB_HPP

// Stata's embedding API, loaded at runtime.
//
// Stata 17 and later ship the whole of Stata as a shared library next to the
// GUI executable -- mp-64.dll / se-64.dll / be-64.dll on Windows,
// libstata-mp.so / libstata-se.so / libstata.so on Linux, and
// Stata<ED>.app/Contents/MacOS/libstata-<ed>.dylib on macOS -- exporting a
// small C interface (StataSO_*). It is what Stata's own Python integration
// (pystata, <stata>/utilities/pystata) drives through ctypes; StataCorp does
// not document it, so the signatures below are the ones pystata calls it
// with. Like elara's R and carpo's Python it is loaded with LoadLibrary/dlopen
// rather than linked, so callisto builds without Stata and a missing Stata is
// an ordinary error at start-up.
//
// Output is not delivered through a callback: Stata appends everything it
// prints to an internal buffer, which StataSO_GetOutputBuffer() returns and
// empties. It may be called from another thread while StataSO_Execute() is
// running (pystata streams output exactly that way), which is how callisto
// streams output too.

#include <string>
#include <vector>

namespace callisto { namespace stata {

    struct Library
    {
        // The shared library that was loaded.
        std::string path;
        // "mp", "se" or "be".
        std::string edition;
        // Every edition whose library is in the directory. A Linux install
        // has all three while the license is for one, so loading the first
        // can pick one the license does not cover.
        std::vector<std::string> installed;
    };

    // Finds Stata's shared library under stataHome -- for `edition` if it is
    // not empty, otherwise the first of mp, se, be that is installed -- loads
    // it and resolves every function below. Throws std::runtime_error saying
    // what was looked for if that fails. Only the first successful call does
    // anything.
    Library loadStataApi(const std::string& stataHome, const std::string& edition);

    bool isStataApiLoaded();

} }

extern "C" {
    using StataSO_Main_t = int (*)(int argc, char** argv);
    // echo: whether Stata prints the command (". cmd") before its output.
    using StataSO_Execute_t = int (*)(const char* command, int echo);
    using StataSO_GetOutputBuffer_t = char* (*)(void);
    using StataSO_ClearOutputBuffer_t = void (*)(void);
    using StataSO_SetBreak_t = void (*)(void);
    using StataSO_Shutdown_t = void (*)(void);
}

namespace callisto { namespace stata { namespace api {
    extern StataSO_Main_t p_StataSO_Main;
    extern StataSO_Execute_t p_StataSO_Execute;
    extern StataSO_GetOutputBuffer_t p_StataSO_GetOutputBuffer;
    extern StataSO_ClearOutputBuffer_t p_StataSO_ClearOutputBuffer;
    extern StataSO_SetBreak_t p_StataSO_SetBreak;
    extern StataSO_Shutdown_t p_StataSO_Shutdown;
} } }

#define StataSO_Main (::callisto::stata::api::p_StataSO_Main)
#define StataSO_Execute (::callisto::stata::api::p_StataSO_Execute)
#define StataSO_GetOutputBuffer (::callisto::stata::api::p_StataSO_GetOutputBuffer)
#define StataSO_ClearOutputBuffer (::callisto::stata::api::p_StataSO_ClearOutputBuffer)
#define StataSO_SetBreak (::callisto::stata::api::p_StataSO_SetBreak)
#define StataSO_Shutdown (::callisto::stata::api::p_StataSO_Shutdown)

#endif
