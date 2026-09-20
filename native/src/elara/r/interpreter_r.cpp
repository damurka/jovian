#include "elara/interpreter_r.hpp"
#include "adrastea/helper.hpp"
#include "adrastea/input.hpp"

#ifdef _MSC_VER
#define _Complex
#endif

#define STRICT_R_HEADERS

// r_dynlib.hpp includes Rinterface.h itself (non-Windows only, with
// R_INTERFACE_PTRS defined) ahead of its own macro section -- see its file
// comment for why that has to happen there and not here.
#include "elara/r/r_dynlib.hpp"
#include "Rversion.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include "elara/r/rtools.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
// windows.h #defines ReadConsole -> ReadConsoleA and WriteConsole ->
// WriteConsoleA, which silently rewrites structRstart's ReadConsole/
// WriteConsole callback fields (R_ext/RStartup.h, included earlier via
// r_dynlib.hpp, so its declarations kept the real names) into names that
// don't exist. Nothing in this file calls the Win32 console API.
#undef ReadConsole
#undef WriteConsole
#endif

#ifdef _MSC_VER
// Clean up our trick so we don't pollute the rest of the C++ project
#undef _Complex
#endif

namespace elara
{
    static RInterpreter* p_interpreter = nullptr;
    RInterpreter* getRInterpreter()
    {
        return p_interpreter;
    }
}

// adrastea::getRegisteredInterpreter()/registerInterpreter()/getInterpreter()
// (the framework's interpreter registry) used to be defined here rather
// than in core/execution/interpreter.cpp, with getInterpreter() falling
// back to elara::getRInterpreter() -- a dependency from the framework back
// onto this specific product that made Adrastea impossible to extract into
// its own target/library. Moved to interpreter.cpp (with that fallback
// dropped, not ported -- see its comment) as part of that extraction; this
// file only keeps elara's own getRInterpreter() accessor below, which
// routine.cpp's R routine callbacks use directly.

namespace elara
{

    void WriteConsoleEx(const char* buf, int buflen, int otype) {
        std::string output(buf, buflen);
        if (otype == 1) {
            p_interpreter->publishStream("stderr", output);
        }
        else {
            p_interpreter->publishStream("stdout", output);
        }
    }

    void captureWriteConsoleEx(const char* buf, int buflen, int otype) {
        std::string output(buf, buflen);
        if (otype == 1) {
            // do nothing
        }
        else {
            p_interpreter->capture_stream << output;
        }
    }

    int ReadConsole(const char* prompt, unsigned char* buffer, int length, int /*addtohistory*/) {
        std::string res;
        try
        {
            res = adrastea::blockingInputRequest(prompt, false, p_interpreter->allowsStdin());
        }
        catch (const std::exception& e)
        {
            // Deliberately not letting this propagate: R's evaluator calls
            // this callback directly through a raw function pointer, and
            // R's own internals aren't C++-exception-safe to unwind
            // through (the same reason evalRString()/executeRequestImpl()
            // use R_tryEval()/R_tryCatchError() instead of a raw Rf_eval()
            // elsewhere in this file). Reporting via publishStream() --
            // already proven safe to call from an R callback, same as
            // WriteConsoleEx() -- and returning 0 (R's own "no more
            // input"/EOF signal for this callback) lets R's normal
            // readline()/scan() error handling take over from here,
            // instead of risking undefined behavior.
            p_interpreter->publishStream("stderr", std::string("input: ") + e.what() + "\n");
            return 0;
        }

        // R's ReadConsole contract: `buffer` (of `length` bytes) receives a
        // NUL-terminated line, conventionally ending in '\n'. This used to
        // copy up to `length` bytes and then write '\n' at buffer[size] --
        // one byte past the end when the reply was `length` bytes or longer
        // -- and never NUL-terminated at all, so R's strlen() over the
        // buffer could read stale bytes left over from an earlier, longer
        // line. Leaves room for both the newline and the terminator.
        if (length < 2)
        {
            return 0;
        }
        std::size_t size = std::min(res.size(), std::size_t(length - 2));
        std::copy(res.c_str(), res.c_str() + size, buffer);
        buffer[size] = '\n';
        buffer[size + 1] = '\0';

        return 1;
    }

#ifdef _WIN32
    // Windows R has no ptr_R_ReadConsole to assign after the fact (R.dll
    // exports R_ReadConsole/R_WriteConsole(Ex) as plain functions, not
    // hookable pointers -- confirmed by inspecting its export table), so
    // the only way to intercept console input is the documented embedding
    // sequence ("Writing R Extensions" 8.2.2, R's own rtest.c): fill in an
    // Rstart, install callbacks through it, R_SetParams(), then
    // setup_Rmainloop(). That's what Rf_initEmbeddedR() does internally
    // too, just with R's own terminal callbacks instead of ours -- which,
    // for input, means reading real keyboard input from the hidden
    // AllocConsole() window nothing can type into, so readline()/scan()
    // used to block forever with no way to ever answer them.
    void noopCallBack() {}
    void showMessageCallback(const char* message)
    {
        std::fprintf(stderr, "[R] %s\n", message ? message : "");
    }
    // 0 = Cancel (1 Yes, -1 No) -- there's no one to ask.
    int yesNoCancelCallback(const char*) { return 0; }
    void busyCallback(int) {}

    void initEmbeddedRWindows(int argc, char* argv[])
    {
        // R keeps Rp->rhome/home as raw pointers (R_SetWin32 stores them
        // into globals), so they must outlive this call -- static storage.
        static std::string rHome;
        static std::string rUser;
        // structRstart itself is only read by R_SetParams(), not kept, but
        // static costs nothing and rules the question out.
        static structRstart rp;

        r::api::p_R_setStartTime();
        r::api::p_R_DefParamsEx(&rp, RSTART_VERSION);

        // Parses (and removes from the copy) the same options
        // Rf_initialize_R() would: --quiet, --no-save, --no-restore, ...
        int ac = argc;
        std::vector<char*> av(argv, argv + argc);
        r::api::p_R_common_command_line(&ac, av.data(), &rp);

        const char* homeFromR = r::api::p_get_R_HOME();
        if (homeFromR && *homeFromR)
        {
            rHome = homeFromR;
        }
        else if (const char* homeFromEnv = std::getenv("R_HOME"))
        {
            rHome = homeFromEnv;
        }
        else
        {
            throw std::runtime_error("R_HOME is not set and R could not locate itself -- cannot start R.");
        }
        const char* userFromR = r::api::p_getRUser();
        rUser = (userFromR && *userFromR) ? userFromR : rHome;
        rp.rhome = rHome.data();
        rp.home = rUser.data();

        rp.CharacterMode = LinkDLL;
        // Interactive is what makes R's readline() actually call
        // ReadConsole instead of returning "" immediately -- see
        // do_readln() in R's scan.c. Safe: our ReadConsole answers EOF
        // (and reports why on stderr) whenever the current execute_request
        // didn't opt into stdin, rather than blocking.
        rp.R_Interactive = 1;
        rp.ReadConsole = ReadConsole;
        rp.WriteConsole = nullptr;
        rp.WriteConsoleEx = WriteConsoleEx;
        rp.CallBack = noopCallBack;
        rp.ShowMessage = showMessageCallback;
        rp.YesNoCancel = yesNoCancelCallback;
        rp.Busy = busyCallback;

        r::api::p_R_SetParams(&rp);
        r::api::p_R_set_command_line_arguments(argc, argv);

        // graphapp initialization -- R's own rtest.c calls this at exactly
        // this point, and Rf_initEmbeddedR()/Rf_initialize_R() do it
        // internally. Without it, anything that touches a GDI-backed
        // graphics device (hera's default device on Windows is png(),
        // whose "windows" bitmap type is graphapp underneath) crashes the
        // whole R process on first use: confirmed directly, plot(1:10)
        // killed the kernel with this omitted and works with it. Lives in
        // Rgraphapp.dll, not R.dll (a dependency of R.dll, so already
        // loaded by now). Non-fatal if it can't be found: R still starts,
        // it's only graphics that would then be unsafe.
        using GA_initapp_t = int (*)(int, char**);
        if (HMODULE graphapp = ::GetModuleHandleA("Rgraphapp.dll"))
        {
            if (auto gaInit = reinterpret_cast<GA_initapp_t>(::GetProcAddress(graphapp, "GA_initapp")))
            {
                gaInit(0, nullptr);
            }
        }

        r::api::p_setup_Rmainloop();
    }
#endif

    RInterpreter::RInterpreter(int argc, char* argv[])
    {
        // Resolves R.dll (Windows) via LoadLibrary/GetProcAddress before
        // anything below touches a single R symbol -- see
        // r/r_dynlib.hpp's file comment. Throws std::runtime_error with a
        // specific, actionable message ("Could not load R.dll ...") if R
        // isn't installed or R_HOME/PATH don't point at a working one;
        // Server::start() (engine.cpp) catches that and reports it as a
        // real startup failure instead of a misleading "ready" signal.
        // A no-op on non-Windows, which still links against libR normally.
        r::loadRApi();

#ifdef _WIN32
        if (AllocConsole()) {
            HWND hwnd = GetConsoleWindow();
            if (hwnd != NULL) {
                ShowWindow(hwnd, SW_HIDE);
            }
            // CRITICAL FIX: Bind the MSVC C-Runtime streams to the new console!
            // Without this, internal C printf() calls in packages (like Shiny/httpuv)
            // will hit a NULL handle and segfault (0xC0000005).
            FILE* fp;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
        }
#endif

        // Debug: Print environment before R init
        printf("[R Interpreter BEFORE Init] R_HOME=%s\n", getenv("R_HOME") ? getenv("R_HOME") : "NOT SET");
        printf("[R Interpreter BEFORE Init] R_LIBS=%s\n", getenv("R_LIBS") ? getenv("R_LIBS") : "NOT SET");
        fflush(stdout);

        // No R_CStackLimit override needed here (unlike an earlier version
        // of this code, which queried this thread's stack bounds and
        // computed one manually): Rf_initEmbeddedR() runs directly on this
        // process's actual main thread now (see Server::start(),
        // engine.cpp), which is exactly what R's own built-in
        // stack-bounds auto-detection assumes and correctly handles --
        // matching how xeus-r's interpreter constructor embeds R, with
        // nothing beyond this one call. That auto-detection reflects
        // whatever real stack this thread has, though, so the linker's
        // default 1MB reserve was still enlarged (native/CMakeLists.txt,
        // the elara target's /STACK option) to actually give it room
        // for the deep C-level recursion real workloads can hit -- the
        // previous manual override was compensating for running on a
        // *secondary* thread (a carryover from this code's Node-addon
        // era), where that auto-detection is simply wrong, not for R
        // needing help in general.
        //
        // Rf_initEmbeddedR() itself, and the printfs bracketing it, are
        // NOT Windows-specific -- this is R's standard, portable embedding
        // API (Rembedded.h), the same call xeus-r's own interpreter
        // constructor makes on every platform it supports. It used to sit
        // inside the #ifdef _WIN32 block above (only the console-allocation
        // code right before it is actually Windows-only), which meant R was
        // silently never initialized at all on Linux/macOS -- undefined
        // behavior from there on (registerRRoutines() and everything after
        // touches R-internal state Rf_initEmbeddedR() sets up), observed
        // directly as SessionRegistryTest hanging at exactly this point
        // (the first real-kernel test, [elara::Server] logging
        // "setup_environment() completed" and then nothing further) the
        // first time this was ever run on those platforms.
#ifdef _WIN32
        if (r::hasWindowsEmbeddingApi())
        {
            initEmbeddedRWindows(argc, argv);
        }
        else
        {
            // R older than 4.2 (no R_DefParamsEx): still starts, but with
            // R's own terminal callbacks, so readline()/scan() can't be
            // answered through the stdin channel on this R version.
            Rf_initEmbeddedR(argc, argv);
        }
#else
        Rf_initEmbeddedR(argc, argv);
#endif

        printf("[R Interpreter AFTER Init] Rf_initEmbeddedR completed\n");
        fflush(stdout);

        registerRRoutines();

#ifndef _WIN32
        // Unix hooks console I/O by assigning libR's exported ptr_R_*
        // function pointers after the fact (Rinterface.h's R_INTERFACE_PTRS
        // mechanism). Windows has no such pointers -- R.dll doesn't export
        // them, confirmed by inspecting its export table -- and hooks the
        // same ReadConsole/WriteConsoleEx through the documented Rstart
        // startup sequence instead: see initEmbeddedRWindows() above.
        ptr_R_WriteConsole = nullptr;
        ptr_R_WriteConsoleEx = WriteConsoleEx;
        ptr_R_ReadConsole = ReadConsole;
        R_Outputfile = NULL;
        R_Consolefile = NULL;

        // Without this, R's readline() never reaches ReadConsole at all:
        // do_readln() (R's scan.c) only reads the console when
        // R_Interactive is set, and otherwise just returns "" -- and an
        // embedded R started without a terminal on stdin (which is always
        // the case under themisto) isn't interactive. Windows gets the
        // same setting through Rstart::R_Interactive. Safe: ReadConsole
        // above answers EOF, with the reason on stderr, whenever the
        // current execute_request didn't opt into stdin, rather than
        // blocking. Lenient: a libR that somehow doesn't export the symbol
        // just keeps its old behavior instead of failing to start.
        if (r::api::p_R_Interactive)
        {
            *r::api::p_R_Interactive = 1;
        }
#endif

        adrastea::registerInterpreter(this);
        p_interpreter = this;
    }

    // Parses and evaluates a string of R code, one top-level expression at a
    // time, via R_tryEval so an R-level error can't longjmp past our C++
    // stack. Returns the value of the last expression (or R_NilValue on a
    // parse failure); the caller is responsible for PROTECTing the result
    // if it outlives this call.
    static SEXP evalRString(const std::string& code, bool* had_error = nullptr)
    {
        if (had_error) *had_error = false;

        SEXP code_sexp = PROTECT(Rf_mkString(code.c_str()));
        ParseStatus status;
        SEXP parsed = PROTECT(R_ParseVector(code_sexp, -1, &status, R_NilValue));

        SEXP result = R_NilValue;
        if (status == PARSE_OK) {
            int n = Rf_length(parsed);
            for (int i = 0; i < n; i++) {
                int error_occurred = 0;
                result = R_tryEval(VECTOR_ELT(parsed, i), R_GlobalEnv, &error_occurred);
                if (error_occurred && had_error) *had_error = true;
            }
        } else if (had_error) {
            *had_error = true;
        }

        UNPROTECT(2);
        return result;
    }

    // Evaluates an execute_request's `user_expressions` ({name: "expr"}) in
    // the global environment and returns the reply-shaped result: per name,
    // {status:"ok", data:{"text/plain": ...}, metadata:{}} or
    // {status:"error", ename, evalue, traceback:[]}. One bad expression must
    // not sink the others (or the execution itself), so each is evaluated
    // separately and R-level failures come back as that expression's error.
    static adrastea::json evalUserExpressions(const adrastea::json& user_expressions)
    {
        adrastea::json out = adrastea::json::object();
        if (!user_expressions.is_object() || user_expressions.empty()) {
            return out;
        }

        SEXP fn = PROTECT(evalRString(
            "function(expr) tryCatch({"
            "  v <- eval(parse(text = expr, keep.source = FALSE), envir = globalenv());"
            "  c('ok', paste(utils::capture.output(print(v)), collapse = '\\n'), '')"
            "}, error = function(e) c('error', class(e)[1], conditionMessage(e)))"));

        for (auto it = user_expressions.begin(); it != user_expressions.end(); ++it) {
            const std::string expr = it.value().is_string() ? it.value().get<std::string>() : std::string();

            SEXP expr_ = PROTECT(Rf_mkString(expr.c_str()));
            SEXP call = PROTECT(r::rCall(fn, expr_));
            int error_occurred = 0;
            SEXP res = PROTECT(R_tryEval(call, R_GlobalEnv, &error_occurred));

            if (error_occurred || !Rf_isString(res) || Rf_length(res) < 3) {
                out[it.key()] = { { "status", "error" }, { "ename", "EvaluationError" },
                                  { "evalue", "could not evaluate expression" }, { "traceback", adrastea::json::array() } };
            } else if (std::string(CHAR(STRING_ELT(res, 0))) == "ok") {
                out[it.key()] = { { "status", "ok" },
                                  { "data", { { "text/plain", std::string(CHAR(STRING_ELT(res, 1))) } } },
                                  { "metadata", adrastea::json::object() } };
            } else {
                out[it.key()] = { { "status", "error" }, { "ename", std::string(CHAR(STRING_ELT(res, 1))) },
                                  { "evalue", std::string(CHAR(STRING_ELT(res, 2))) },
                                  { "traceback", adrastea::json::array() } };
            }
            UNPROTECT(3);
        }

        UNPROTECT(1);
        return out;
    }

    void RInterpreter::configureImpl()
    {
        // Debug: Print R environment variables
        printf("[R Interpreter] R_HOME=%s\n", getenv("R_HOME") ? getenv("R_HOME") : "NOT SET");
        printf("[R Interpreter] R_LIBS=%s\n", getenv("R_LIBS") ? getenv("R_LIBS") : "NOT SET");
        fflush(stdout);

#ifdef _WIN32
        // Windows R defaults its "native encoding" to the system codepage
        // unless told otherwise, which triggers spurious "strings not
        // representable in native encoding will be translated to UTF-8"
        // warnings for any UTF-8 content (e.g. i18n translations in a Shiny
        // app). R 4.2+ on Windows 10 1903+ can use UTF-8 as its native
        // encoding directly -- silently a no-op on older combinations.
        // R itself warns that switching off the system codepage "may cause
        // problems" -- that's the tradeoff we're intentionally making here
        // (one expected warning instead of many unpredictable encoding
        // ones), and R's default warning buffering meant it wasn't even
        // showing up until interpreter teardown, looking unrelated to its
        // actual cause. Suppress it rather than let it surface confusingly.
        evalRString("suppressWarnings(try(Sys.setlocale('LC_ALL', '.UTF-8'), silent = TRUE))");
#endif

        // Debug: Print .libPaths() from R
        SEXP get_libpaths = PROTECT(Rf_lang1(Rf_install(".libPaths")));
        SEXP libpaths = PROTECT(Rf_eval(get_libpaths, R_GlobalEnv));
        printf("[R Interpreter] .libPaths() count: %d\n", Rf_length(libpaths));
        for (int i = 0; i < Rf_length(libpaths); i++) {
            printf("[R Interpreter] .libPaths()[%d] = %s\n", i, CHAR(STRING_ELT(libpaths, i)));
        }
        fflush(stdout);
        UNPROTECT(2);

        // Try to load hera, auto-installing from the bundled source (via
        // remotes::install_local, into the already-configured R_LIBS path)
        // if it's missing -- MAKE IT OPTIONAL FOR NOW, still don't throw if
        // it ultimately can't be loaded.
        printf("[R Interpreter] Attempting to load 'hera' package...\n");
        fflush(stdout);

        // Beyond "missing", an already-installed 'hera' can also be STALE: a
        // previous session's remotes::install_local() left a compiled copy
        // in the library, and since its DESCRIPTION Version doesn't change
        // between dev iterations, a plain require("hera") would keep
        // silently loading that stale copy forever even after the source
        // under ELARA_HERA_SRC changes -- exactly what happened here
        // (an old display_data() that charToRaw()'d its JSON payload before
        // the .Call(), crashing the C side with "STRING_ELT() ... not a
        // 'raw'" on every plot, while the fixed source on disk was never
        // reinstalled). Comparing source file mtimes against the installed
        // DESCRIPTION's mtime catches that without needing a version bump
        // on every edit.
        static const char* load_hera_code = R"(
            local({
                status <- "missing"
                hera_src <- Sys.getenv("ELARA_HERA_SRC", unset = "")
                has_source <- nzchar(hera_src) && dir.exists(hera_src)

                installed_path <- tryCatch(find.package("hera", quiet = TRUE), error = function(e) character(0))
                is_installed <- length(installed_path) > 0

                is_stale <- FALSE
                if (has_source && is_installed) {
                    installed_desc <- file.path(installed_path, "DESCRIPTION")
                    src_files <- list.files(file.path(hera_src, "R"), full.names = TRUE, pattern = "\\.[Rr]$")
                    src_files <- c(src_files, file.path(hera_src, "DESCRIPTION"), file.path(hera_src, "NAMESPACE"))
                    src_files <- src_files[file.exists(src_files)]
                    if (file.exists(installed_desc) && length(src_files) > 0) {
                        installed_mtime <- file.info(installed_desc)$mtime
                        source_mtime <- max(file.info(src_files)$mtime)
                        is_stale <- source_mtime > installed_mtime
                    }
                }

                needs_install <- has_source && (!is_installed || is_stale)

                if (needs_install && requireNamespace("remotes", quietly = TRUE)) {
                    install_ok <- tryCatch({
                        remotes::install_local(hera_src, upgrade = "never", quiet = TRUE, force = TRUE)
                        TRUE
                    }, error = function(e) FALSE)
                    if (install_ok && suppressWarnings(require("hera", quietly = TRUE))) {
                        status <- if (is_stale) "reinstalled_stale" else "auto_installed"
                    } else {
                        status <- "install_failed"
                    }
                } else if (suppressWarnings(require("hera", quietly = TRUE))) {
                    status <- "already_loaded"
                } else if (!has_source) {
                    status <- if (!nzchar(hera_src)) "no_source_configured" else "source_not_found"
                } else {
                    status <- "remotes_unavailable"
                }
                status
            })
        )";

        bool had_error = false;
        SEXP out = PROTECT(evalRString(load_hera_code, &had_error));

        std::string status = (!had_error && Rf_isString(out) && Rf_length(out) > 0)
            ? CHAR(STRING_ELT(out, 0))
            : "error";

        if (status == "already_loaded") {
            printf("[R Interpreter] Successfully loaded 'hera' package\n");
        } else if (status == "auto_installed") {
            printf("[R Interpreter] 'hera' was not installed -- auto-installed from ELARA_HERA_SRC and loaded successfully\n");
        } else if (status == "reinstalled_stale") {
            printf("[R Interpreter] Installed 'hera' was older than ELARA_HERA_SRC -- reinstalled and loaded successfully\n");
        } else {
            printf("[R Interpreter] WARNING: 'hera' package could not be loaded (status: %s). Some features may not work.\n", status.c_str());
            printf("[R Interpreter] Continuing without 'hera' for testing purposes...\n");
            // DON'T throw - just warn for now
            // throw std::runtime_error(
            //     "Fatal Initialization Error: The mandatory partner library package 'hera' "
            //     "could not be loaded. Please ensure 'hera' is correctly installed."
            // );
        }
        fflush(stdout);

        UNPROTECT(1);
    }

    void RInterpreter::executeRequestImpl(
        send_reply_callback cb,
        int execution_count,
        const std::string& code,
        adrastea::ExecuteRequestConfig config,
        adrastea::json user_expressions
    )
    {
        struct ExecutingScope {
            std::atomic<bool>& flag;
            explicit ExecutingScope(std::atomic<bool>& f) : flag(f) { flag = true; }
            ~ExecutingScope() { flag = false; }
        } executing(m_executing);

        SEXP code_ = PROTECT(Rf_mkString(code.c_str()));
        SEXP execution_counter_ = PROTECT(Rf_ScalarInteger(execution_count));
        SEXP silent_ = PROTECT(Rf_ScalarLogical(config.silent));
        SEXP result = PROTECT(r::invokeHeraFn("execute", code_, execution_counter_, silent_));

        if (Rf_inherits(result, "error_reply")) {
            // Matches hera's own construction order exactly (packages/hera/R/execute.R's
            // handle_error(): structure(list(ename = ..., evalue = ..., trace_back), ...)) --
            // these two used to be extracted with their names swapped relative to these
            // indices, compensated for by also swapping them in the publishExecutionError()/
            // createErrorReply() calls below (both take (ename, evalue, ...)) -- net
            // behavior was already correct, but it was one accidental un-swap away from
            // silently reporting every R error with its class name and message flipped.
            std::string ename = CHAR(STRING_ELT(VECTOR_ELT(result, 0), 0));
            std::string evalue = CHAR(STRING_ELT(VECTOR_ELT(result, 1), 0));

            std::vector<std::string> trace_back;
            if (XLENGTH(result) > 2) {
                SEXP trace_back_ = VECTOR_ELT(result, 2);
                auto n = XLENGTH(trace_back_);
                for (decltype(n) i = 0; i < n; i++) {
                    trace_back.push_back(CHAR(STRING_ELT(trace_back_, i)));
                }
            }

            publishExecutionError(ename, evalue, trace_back);
            cb(adrastea::createErrorReply(ename, evalue, std::move(trace_back)));
        }
        else {
           if (Rf_inherits(result, "execution_result")) {
                SEXP data_ = VECTOR_ELT(result, 0);
                SEXP metadata_ = VECTOR_ELT(result, 1);
                auto data = adrastea::json::parse(CHAR(STRING_ELT(data_, 0)));
                auto metadata = adrastea::json::parse(CHAR(STRING_ELT(metadata_, 0)));
                publishExecutionResult(execution_count, data, metadata);
            }

            // Evaluated after the code itself, and only on success, per the
            // spec (a failed execution's reply carries no user_expressions).
            cb(adrastea::createSuccessfulReply(adrastea::json::array(), evalUserExpressions(user_expressions)));
        }

        UNPROTECT(4);
    }

    adrastea::json RInterpreter::isCompleteRequestImpl(const std::string& code_)
    {
        SEXP code = PROTECT(Rf_mkString(code_.c_str()));

        R_tryCatchError(
            [](void* void_code) { // body
                ParseStatus status;
                SEXP code = reinterpret_cast<SEXP>(void_code);

                R_ParseVector(code, -1, &status, R_NilValue);

                switch (status) {
                case PARSE_INCOMPLETE:
                    SET_STRING_ELT(code, 0, Rf_mkChar("incomplete"));
                    break;
                case PARSE_ERROR:
                    SET_STRING_ELT(code, 0, Rf_mkChar("invalid"));
                    break;
                default:
                    SET_STRING_ELT(code, 0, Rf_mkChar("complete"));
                }

                return R_NilValue;
            },
            reinterpret_cast<void*>(code),

            [](SEXP, void* void_code) { // handler
                SEXP code = reinterpret_cast<SEXP>(void_code);
                SET_STRING_ELT(code, 0, Rf_mkChar("invalid"));

                return R_NilValue;
            },
            reinterpret_cast<void*>(code)
        );

        adrastea::json res = adrastea::createIsCompleteReply(CHAR(STRING_ELT(code, 0)), "");
        UNPROTECT(1);
        return res;
    }

    adrastea::json jsonFromCharacterVector(SEXP x) {
        auto n = XLENGTH(x);
        std::vector<std::string> vec(n);

        for (decltype(n) i = 0; i < n; i++) {
            vec[i] = std::string(CHAR(STRING_ELT(x, i)));
        }
        return adrastea::json(vec);
    }

    adrastea::json RInterpreter::completeRequestImpl(const std::string& code, int cursor_pos)
    {
        SEXP code_ = PROTECT(Rf_mkString(code.c_str()));
        SEXP cursor_pos_ = PROTECT(Rf_ScalarInteger(cursor_pos));
        SEXP result = PROTECT(r::invokeHeraFn("complete", code_, cursor_pos_));

        auto matches = jsonFromCharacterVector(VECTOR_ELT(result, 0));
        int cursor_start = INTEGER_ELT(VECTOR_ELT(result, 1), 0);
        int cursor_end = INTEGER_ELT(VECTOR_ELT(result, 1), 1);

        UNPROTECT(3);
        return adrastea::createCompleteReply(matches, cursor_start, cursor_end);
    }

    adrastea::json RInterpreter::inspectRequestImpl(const std::string& code, int cursor_pos, int /*detail_level*/)
    {
        SEXP code_ = PROTECT(Rf_mkString(code.c_str()));
        SEXP cursor_pos_ = PROTECT(Rf_ScalarInteger(cursor_pos));
        SEXP result = PROTECT(r::invokeHeraFn("inspect", code_, cursor_pos_));

        bool found = LOGICAL_ELT(VECTOR_ELT(result, 0), 0);
        if (!found) {
            UNPROTECT(3);
            return adrastea::createInspectReply(false);
        }

        auto data = adrastea::json::parse(CHAR(STRING_ELT(VECTOR_ELT(result, 1), 0)));
        UNPROTECT(3);
        return adrastea::createInspectReply(found, data);
    }

    adrastea::json RInterpreter::shutdownRequestImpl(bool restart)
    {
        Rf_endEmbeddedR(0);
        return adrastea::createShutdownReply(restart);
    }

    adrastea::json RInterpreter::interruptRequestImpl()
    {
        // Runs on the kernel's control-channel thread while the R thread is
        // busy executing (see adrastea::ServerZmqImpl's control watcher), so
        // it may only do thread-safe things: flag the interrupt and let R
        // itself unwind at its next check. Nothing to break when idle -- the
        // flag would then linger and abort the NEXT execution, so only set
        // it while one is actually running.
        if (m_executing.load()) {
            r::requestRInterrupt();
        }
        return adrastea::createInterruptReply();
    }

    adrastea::json RInterpreter::kernelInfoRequestImpl()
    {
        const std::string  implementation = "xr";
        const std::string  implementation_version{ adrastea::version::kernel_protocol_version };
        const std::string  language_name = "R";
        const std::string  language_version = std::string(R_MAJOR) + "." + std::string(R_MINOR);
        const std::string  language_mimetype = "text/x-R";
        const std::string  language_file_extension = ".R";
        const std::string  language_pygments_lexer = "r";
        const std::string  language_codemirror_mode = "";
        const std::string  language_nbconvert_exporter = "";
        const std::string  banner = "xr";
        const adrastea::json     help_links = adrastea::json::array();

        return adrastea::createInfoReply(
            implementation,
            implementation_version,
            language_name,
            language_version,
            language_mimetype,
            language_file_extension,
            language_pygments_lexer,
            language_codemirror_mode,
            language_nbconvert_exporter,
            banner,
            help_links
        );
    }
}
