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
#include "elara/r/hera_sources.hpp"
#include "elara/r/json_convert.hpp"
#include "elara/log.hpp"
#include "elara/r/debugger_r.hpp"

#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#endif

#include <algorithm>
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

    // `code` with \n line ends: \r\n and a lone \r become \n (R's parser rejects a carriage return)
    std::string withLineFeeds(const std::string& code)
    {
        if (code.find('\r') == std::string::npos) return code;
        std::string out;
        out.reserve(code.size());
        for (size_t i = 0; i < code.size(); ++i) {
            if (code[i] != '\r') out += code[i];
            else if (i + 1 >= code.size() || code[i + 1] != '\n') out += '\n';
        }
        return out;
    }

    void WriteConsoleEx(const char* buf, int buflen, int otype) {
        p_interpreter->writeConsole(buf, buflen, otype);
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
        return p_interpreter->readConsole(prompt, buffer, length);
    }

    int RInterpreter::stdinInput(const char* prompt, unsigned char* buffer, int length) {
        std::string res;
        try
        {
            res = adrastea::blockingInputRequest(prompt, false, allowsStdin());
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
            publishStream("stderr", std::string("input: ") + e.what() + "\n");
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
        // R's alerts (a fatal error at start-up, say), with no one to show them to
        log::warning(std::string("R: ") + (message ? message : ""));
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

    RInterpreter::~RInterpreter() = default;

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
        log::keepCurrentStderr();
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

        log::debug(std::string("starting R: R_HOME=") + (getenv("R_HOME") ? getenv("R_HOME") : "(not set)")
            + ", R_LIBS=" + (getenv("R_LIBS") ? getenv("R_LIBS") : "(not set)"));

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
        // Rf_initEmbeddedR() itself, and the log lines bracketing it, are
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

        log::debug("R started");

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

#ifndef _WIN32
        m_mainThread = pthread_self();
        struct sigaction wake {};
        wake.sa_handler = [](int) {};
        sigemptyset(&wake.sa_mask);
        wake.sa_flags = 0; // no SA_RESTART: the point is to interrupt the blocked call
        sigaction(SIGUSR2, &wake, nullptr);
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
    // The user expression Jovian adds to every cell for its busy-time helper (R_STATE_KEY in lib/session/r-helper.ts):
    // what the session has attached, defined and loaded. Answered here, from search(), ls() and loadedNamespaces(),
    // as {"search": [...], "globals": [...], "loaded": [...]} -- not by evaluating the expression it came with, which
    // would parse, run, print and re-read R code after every cell.
    static const char* const kSessionStateKey = ".jovian_state";

    static adrastea::json sessionState()
    {
        auto strings = [](SEXP x, R_xlen_t max) {
            adrastea::json out = adrastea::json::array();
            if (TYPEOF(x) == STRSXP) {
                for (R_xlen_t i = 0, n = std::min(XLENGTH(x), max); i < n; ++i) {
                    out.push_back(Rf_translateCharUTF8(STRING_ELT(x, i)));
                }
            }
            return out;
        };
        auto call = [](SEXP head, bool withGlobalEnv) {
            SEXP expr = PROTECT(withGlobalEnv ? r::rCall(head, R_GlobalEnv) : r::rCall(head));
            int error = 0;
            SEXP value = R_tryEval(expr, R_GlobalEnv, &error);
            UNPROTECT(1);
            return error || !value ? R_NilValue : value;
        };

        SEXP search = PROTECT(call(Rf_install("search"), false));
        SEXP globals = PROTECT(call(Rf_install("ls"), true));
        // the namespaces loaded, attached or not: their DLLs are in use (on Windows they can't be replaced), so an
        // install replacing one of them waits for this session (SessionManager.ensureRPackage())
        SEXP loaded = PROTECT(call(Rf_install("loadedNamespaces"), false));
        adrastea::json state = {
            { "search", strings(search, R_XLEN_T_MAX) }, { "globals", strings(globals, 5000) }, { "loaded", strings(loaded, R_XLEN_T_MAX) }
        };
        UNPROTECT(3);
        return { { "status", "ok" }, { "data", { { "text/plain", state.dump() } } }, { "metadata", adrastea::json::object() } };
    }

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
            if (it.key() == kSessionStateKey) {
                out[it.key()] = sessionState();
                continue;
            }
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

        if (log::enabled(log::Level::debug)) {
            SEXP get_libpaths = PROTECT(Rf_lang1(Rf_install(".libPaths")));
            SEXP libpaths = PROTECT(Rf_eval(get_libpaths, R_GlobalEnv));
            std::string paths;
            for (int i = 0; i < Rf_length(libpaths); i++) {
                paths += (i ? "; " : "") + std::string(CHAR(STRING_ELT(libpaths, i)));
            }
            log::debug("R libraries: " + (paths.empty() ? std::string("(none)") : paths));
            UNPROTECT(2);
        }

        // hera -- the kernel's own R code: running cells, rich output, comms, completion -- is built into the
        // kernel (hera_sources.hpp) and loaded here, the way Ark carries its R code: nothing is installed, so a
        // first session needs no package install (and no Rscript run to do one), and a session loads only R's base
        // packages besides. An edit to packages/hera takes a rebuild of the kernel.
        //
        // As Ark's tools:positron: one locked environment on the search path, "tools:jovian", every name in it
        // dot-named -- `.jv.*` the kernel's own, `.elara.*` what notebooks and packages call (.elara.display(),
        // .elara.host_ask(), ...) -- so it masks nothing and adds no namespace. Its functions look names up from
        // there outward (R's packages, then base): a user's objects, in the global environment ahead of it, are not
        // on that path. View() is replaced inside utils, as Ark does, rather than masked. The methods of base R's
        // generics (print, $) are registered from NAMESPACE; hera's own generics find theirs beside them.
        static const char* hera_loader = R"hera(
            function(paths, texts) {
                # R's parser rejects a carriage return, which a CRLF checkout (Windows runners) puts in the built-in files
                texts <- gsub("\r", "", texts, fixed = TRUE)
                text_of <- function(path) texts[[match(path, paths)]]
                description <- text_of("DESCRIPTION")
                version <- read.dcf(textConnection(description), fields = "Version")[1, 1]
                directives <- strsplit(text_of("NAMESPACE"), "\n", fixed = TRUE)[[1]]
                # S3method(generic, class, function)
                methods <- regmatches(directives, regexec("^S3method\\(([^,)]+),([^,)]+),([^,)]+)\\)", directives))
                methods <- lapply(Filter(length, methods), function(m) gsub("[\"`]", "", m[-1]))

                env <- attach(NULL, pos = 2L, name = "tools:jovian")
                for (file in paths[startsWith(paths, "R/")]) {
                    for (e in parse(text = text_of(file), keep.source = FALSE, encoding = "UTF-8")) eval(e, env)
                }
                assign(".elara.version", unname(version), envir = env)
                # nothing that could mask: every name dot-named
                plain <- grep("^[^.]", ls(env, all.names = TRUE), value = TRUE)
                if (length(plain)) stop("names in tools:jovian without a dot: ", paste(plain, collapse = ", "), call. = FALSE)

                for (m in methods) registerS3method(m[[1]], m[[2]], get(m[[3]], envir = env), envir = baseenv())
                # View(): the kernel's, in utils itself (and on package:utils, as attached), as Ark replaces it
                unlock <- get("unlockBinding", envir = baseenv())
                lock <- get("lockBinding", envir = baseenv())
                for (where in c("namespace:utils", "package:utils")) {
                    target <- if (where == "namespace:utils") asNamespace("utils") else if (where %in% search()) as.environment(where)
                    if (!is.null(target) && exists("View", envir = target, inherits = FALSE)) {
                        unlock("View", target)
                        assign("View", get(".elara.View", envir = env), envir = target)
                        lock("View", target)
                    }
                }
                get(".jv.onLoad", envir = env)(NULL, "elara")
                lockEnvironment(env, bindings = TRUE)

                paste0("hera ", version, " (built in) as tools:jovian")
            }
        )hera";

        const auto& files = hera::sourceFiles();
        SEXP paths = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(files.size())));
        SEXP texts = PROTECT(Rf_allocVector(STRSXP, static_cast<R_xlen_t>(files.size())));
        for (size_t i = 0; i < files.size(); ++i) {
            SET_STRING_ELT(paths, static_cast<R_xlen_t>(i), Rf_mkChar(files[i].path));
            SET_STRING_ELT(texts, static_cast<R_xlen_t>(i), Rf_mkCharLenCE(files[i].text.data(), static_cast<int>(files[i].text.size()), CE_UTF8));
        }

        bool had_error = false;
        SEXP loader = PROTECT(evalRString(hera_loader, &had_error));
        int load_error = 1;
        SEXP out = R_NilValue;
        if (!had_error) {
            SEXP call = PROTECT(r::rCall(loader, paths, texts));
            out = R_tryEval(call, R_GlobalEnv, &load_error);
            UNPROTECT(1);
        }
        PROTECT(out);

        if (!load_error && Rf_isString(out) && Rf_length(out) > 0) {
            log::info(std::string("loaded ") + CHAR(STRING_ELT(out, 0)));
        } else {
            // without hera nothing can be run; said loudly, but the kernel still starts so the error can be seen
            log::error(std::string("the kernel's R code (hera) could not be loaded: ") + R_curErrorBuf());
        }

        // the print methods of values only a frontend shows (see hera's repl.R); R's global error handler is
        // installed by R's console loop itself (readConsole()): from here it would last only as long as this call
        bool install_error = false;
        evalRString("local({ env <- as.environment(\"tools:jovian\"); env$.jv.display.install(); env$.jv.ui.install() })", &install_error);
        if (install_error) {
            log::error(std::string("installing the display overrides: ") + R_curErrorBuf());
        }

        // traceback() reads .Traceback from R's base environment, which hera sets after a cell's error. R creates
        // that binding itself, when an error reaches a top-level context through its default handler -- never the
        // case for a cell's error, which hera catches -- and an R-level assign() cannot add it (base is locked). So
        // one such error now, with error messages off, creates it for hera to set.
        bool traceback_error = false;
        evalRString("local({ op <- options(show.error.messages = FALSE); on.exit(options(op)); stop('') })", &traceback_error);
        if (!traceback_error) {
            log::warning("could not prepare traceback() for cells' errors");
        }

        UNPROTECT(4);
    }

    // R's console loop and the cell: see the comment on readConsole().
    void RInterpreter::attachServer(adrastea::Server* server)
    {
        m_server = server;
        if (!m_debugger) m_debugger = std::make_unique<RDebugger>(*this);
    }

    adrastea::json RInterpreter::debugRequestImpl(const adrastea::json& request)
    {
        if (!m_debugger) m_debugger = std::make_unique<RDebugger>(*this);
        return m_debugger->request(request);
    }

    bool RInterpreter::takeDebugPause()
    {
        return m_debugger && m_debugger->takePauseRequest();
    }

    void RInterpreter::runMainLoop()
    {
        if (m_debugger) m_debugger->setRThread(std::this_thread::get_id());
        log::debug("running R's console loop");
        // Returns only if R does (it exits the process when its input ends: see readConsole())
        run_Rmainloop();
    }

    // Queues the cell -- run by readConsole(), in R's console loop -- and returns; the reply is sent when it is
    // done. Parsed whole first: a syntax error, or incomplete code, is the cell's error at once.
    void RInterpreter::executeRequestImpl(
        send_reply_callback cb,
        int execution_count,
        const std::string& code,
        adrastea::ExecuteRequestConfig config,
        adrastea::json user_expressions
    )
    {
        m_interruptRequested = false;

        // while debugging, the cell is run from the file Jupyter clients know it by (debugInfo, dumpCell), and lines
        // with breakpoints stop: see debugger_r.cpp
        bool debugging = m_debugger && m_debugger->started();
        std::string path = debugging ? m_debugger->cellPath(code) : std::string();
        // R's parser takes a carriage return for an invalid token: a cell written on Windows (\r\n) is given to R with
        // \n line ends (the same lines, so a breakpoint's line still matches; the debugger names the cell by its text)
        SEXP code_ = PROTECT(Rf_mkString(withLineFeeds(code).c_str()));
        SEXP count_ = PROTECT(Rf_ScalarInteger(execution_count));
        SEXP parsed = R_NilValue;
        try {
            if (debugging) {
                std::vector<int> lines = m_debugger->breakpointLines(path);
                SEXP path_ = PROTECT(Rf_mkString(path.c_str()));
                SEXP lines_ = PROTECT(Rf_allocVector(INTSXP, static_cast<R_xlen_t>(lines.size())));
                for (size_t i = 0; i < lines.size(); ++i) SET_INTEGER_ELT(lines_, static_cast<R_xlen_t>(i), lines[i]);
                parsed = r::invokeHeraFn(".jv.debug.parse", code_, path_, lines_);
                UNPROTECT(2);
                PROTECT(parsed);
            } else {
                parsed = PROTECT(r::invokeHeraFn(".jv.repl.parse", code_, count_));
            }
        } catch (const std::exception& e) {
            UNPROTECT(2);
            cb(adrastea::createErrorReply("InternalError", e.what(), {}));
            return;
        }
        if (TYPEOF(parsed) == STRSXP) {
            std::string message = Rf_translateCharUTF8(STRING_ELT(parsed, 0));
            UNPROTECT(3);
            if (!config.silent) publishExecutionError("PARSE ERROR", message, {});
            cb(adrastea::createErrorReply("PARSE ERROR", message, {}));
            return;
        }

        auto cell = std::make_unique<Cell>();
        cell->reply = std::move(cb);
        cell->executionCount = execution_count;
        cell->silent = config.silent;
        cell->userExpressions = std::move(user_expressions);
        cell->exprs = parsed;
        cell->count = static_cast<long>(Rf_xlength(parsed));
        SETCDR(Rf_install(".jv_cell"), parsed); // alive until the cell is done
        UNPROTECT(3);

        SEXP silent_ = PROTECT(Rf_ScalarLogical(config.silent));
        try {
            r::invokeHeraFn(".jv.repl.cell_start", silent_);
        } catch (const std::exception& e) {
            log::error(std::string("starting a cell: ") + e.what());
        }
        UNPROTECT(1);

        bool prompt_error = false;
        SEXP prompt = PROTECT(evalRString("getOption('prompt')", &prompt_error));
        if (!prompt_error && TYPEOF(prompt) == STRSXP && XLENGTH(prompt) > 0) m_topPrompt = CHAR(STRING_ELT(prompt, 0));
        UNPROTECT(1);
        SEXP continuePrompt = PROTECT(evalRString("getOption('continue')", &prompt_error));
        if (!prompt_error && TYPEOF(continuePrompt) == STRSXP && XLENGTH(continuePrompt) > 0) m_continuePrompt = CHAR(STRING_ELT(continuePrompt, 0));
        UNPROTECT(1);

        m_executing = true;
        m_cell = std::move(cell);
    }

    // R's console asks for input. As in Ark (crates/ark/src/console):
    //
    // - Code asks (readline(), menu(), a browser() prompt -- any prompt but R's top-level one): the frontend answers
    //   through the stdin channel (stdinInput()).
    // - A cell is under way: its next expression is evaluated here with Rf_eval -- not R_tryEval, whose top-level
    //   context drops the global calling handlers (hera's error handler) and restores R_Visible -- its value stored
    //   in base::.jv_last_value, and R is handed that symbol's name (or invisible() of it, as the value was) as its
    //   next line. R's console then does what it does after any top-level expression -- prints a visible value, sets
    //   .Last.value, prints the warnings, runs task callbacks -- and asks again. What it prints for the last
    //   expression is the cell's result (writeConsole()).
    // - An expression that failed (hera's error handler abandons it; or an interrupt, or an error R handled itself)
    //   has jumped back to R's top level, past this function: R asks again with an evaluation still marked running.
    //   As Ark does, R is given one turn first (invisible(.Last.value)) to reset its evaluation state, and the cell
    //   ends with the error. (Safe because nothing here needs destroying when that jump skips it.)
    // - No cell: the server's loop turns (requests, idle work) until one comes. Once the server has stopped, the end
    //   of input: R ends, and the process with it.
    int RInterpreter::readConsole(const char* prompt, unsigned char* buffer, int length)
    {
        // (R's continuation prompt too: a line R took for the start of an expression -- never one of a cell's, which
        // are parsed here -- is ended by the next)
        bool topLevel = prompt && (m_topPrompt == prompt || m_continuePrompt == prompt);
        // R stopped in browser() (a breakpoint, a step) with a debugger attached: the debugger answers
        if (m_server && !topLevel && m_debugger && m_debugger->started() && prompt && std::strncmp(prompt, "Browse[", 7) == 0) {
            return m_debugger->stopped(buffer, length);
        }
        if (!m_server || !topLevel) {
            return stdinInput(prompt, buffer, length);
        }
        m_inTail = false;
        m_captureValue = false;

        if (!m_pendingLine.empty() && static_cast<int>(m_pendingLine.size()) + 1 <= length) {
            std::memcpy(buffer, m_pendingLine.c_str(), m_pendingLine.size() + 1);
            m_pendingLine.clear();
            m_inTail = m_pendingTail;
            m_captureValue = m_pendingCapture;
            return 1;
        }

        if (!m_loopStarted) {
            // R's global calling handlers belong to its top level: installed by its own console loop, as its first
            // line -- through R_tryEval they would last only as long as that call
            m_loopStarted = true;
            static const char install[] = "base::invisible(base::as.environment(\"tools:jovian\")$.jv.errors.install())\n";
            if (static_cast<int>(sizeof install) <= length) {
                std::memcpy(buffer, install, sizeof install);
                m_inTail = true;
                return 1;
            }
        }

        if (m_evaluating) {
            m_evaluating = false;
            if (m_cell) {
                m_cell->failed = true;
                if (!m_cell->errorRecorded && !m_interruptRequested) {
                    // not through hera's handler (a C stack overflow, an error in the handler): R's own words
                    std::string message = R_curErrorBuf();
                    while (!message.empty() && (message.back() == '\n' || message.back() == ' ')) message.pop_back();
                    m_cell->evalue = message;
                }
            }
            static const char recover[] = "base::invisible(base::.Last.value)\n";
            if (static_cast<int>(sizeof recover) <= length) {
                std::memcpy(buffer, recover, sizeof recover);
                m_inTail = true;
                return 1;
            }
        }

        constexpr long kIdleSliceMs = 20;
        for (;;) {
            if (m_cell) {
                if (m_cell->checkGraphics) {
                    // a plot the expression drew, or changed (hera's .jv.graphics.after_expression())
                    m_cell->checkGraphics = false;
                    if (!m_cell->silent) {
                        try { r::invokeHeraFn(".jv.graphics.after_expression"); }
                        catch (const std::exception& e) { log::warning(std::string("checking for a plot: ") + e.what()); }
                    }
                }
                if (!m_cell->failed && !m_interruptRequested && m_cell->next < m_cell->count) {
                    if (evaluateNext(buffer, length)) return 1;
                    continue; // it failed
                }
                finishCell();
                continue;
            }
            if (!m_server->pollOnce(kIdleSliceMs)) {
                return 0;
            }
        }
    }

    // Evaluates the expression with R's own evaluator, in the global environment. An error long-jumps past this
    // and readConsole() (see there): neither may hold anything needing destruction across the call.
    static SEXP evaluateTopLevel(SEXP expr)
    {
        return Rf_eval(expr, R_GlobalEnv);
    }

    bool RInterpreter::evaluateNext(unsigned char* buffer, int length)
    {
        Cell& cell = *m_cell;
        // where in the cell this expression is (the debugger's <cell> frame: hera's .jv.debug.stack())
        SEXP srcrefs = Rf_getAttrib(reinterpret_cast<SEXP>(cell.exprs), Rf_install("srcref"));
        SETCDR(Rf_install(".jv_cell_srcref"), TYPEOF(srcrefs) == VECSXP && cell.next < XLENGTH(srcrefs) ? VECTOR_ELT(srcrefs, cell.next) : R_NilValue);
        SEXP expr = VECTOR_ELT(reinterpret_cast<SEXP>(cell.exprs), cell.next++);
        bool last = cell.next == cell.count;

        m_evaluating = true;
        SEXP value = evaluateTopLevel(expr);
        m_evaluating = false;
        bool visible = *r::api::p_R_Visible != 0;
        SETCDR(Rf_install(".jv_last_value"), value);

        const char* line = visible ? "base::.jv_last_value\n" : "base::invisible(base::.jv_last_value)\n";
        int size = static_cast<int>(std::strlen(line));
        if (size + 1 > length) return false;
        cell.checkGraphics = true;

        if (m_debugger && m_debugger->takeBrowsed()) {
            // R's browser read its commands through R's console input, which R's console then reads on from: as
            // Ark does, first a line of nothing to reset it, and this one at the next turn
            m_pendingLine = line;
            m_pendingTail = true;
            m_pendingCapture = last && !cell.silent;
            static const char reset[] = " \n";
            std::memcpy(buffer, reset, sizeof reset);
            return true;
        }

        std::memcpy(buffer, line, size + 1);
        m_inTail = true;
        m_captureValue = last && !cell.silent;
        return true;
    }

    void RInterpreter::writeConsole(const char* buf, int buflen, int otype)
    {
        if (m_cell && m_cell->silent) return;
        if (otype == 0 && m_debugger && m_debugger->started() && !(m_inTail && m_captureValue)) {
            // while debugging, a line at a time: browser()'s own ("Called from: f()", "debug at file#3: x <- 1")
            // tell the debugger where R is, and are not shown
            m_debugOutput.append(buf, buflen);
            size_t end;
            while ((end = m_debugOutput.find('\n')) != std::string::npos) {
                std::string line = m_debugOutput.substr(0, end + 1);
                m_debugOutput.erase(0, end + 1);
                if (!m_debugger->consoleLine(line.substr(0, end))) {
                    publishStream("stdout", line);
                }
            }
            return;
        }
        if (otype == 0 && m_inTail) {
            // what R's console prints after an expression: the last expression's value is the cell's result;
            // an earlier one's is not shown (as in a notebook, and Ark)
            if (m_captureValue && m_cell) m_cell->valueText.append(buf, buflen);
            return;
        }
        publishStream(otype == 1 ? "stderr" : "stdout", std::string(buf, buflen));
    }

    bool RInterpreter::wantsCellError() const
    {
        return m_cell && !m_cell->errorRecorded && (m_evaluating || m_inTail);
    }

    void RInterpreter::recordCellError(std::string evalue, std::vector<std::string> traceback)
    {
        if (!wantsCellError()) return;
        m_cell->errorRecorded = true;
        m_cell->failed = true;
        m_cell->evalue = std::move(evalue);
        m_cell->traceback = std::move(traceback);
    }

    // The cell is done: the plot sent, the warnings of a failed expression printed, cell_options() undone (hera's
    // .jv.repl.cell_done()), then its result or error, and the reply.
    void RInterpreter::finishCell()
    {
        std::unique_ptr<Cell> cell = std::move(m_cell);
        m_inTail = false;
        m_captureValue = false;
        if (!m_debugOutput.empty() && !cell->silent) publishStream("stdout", m_debugOutput);
        m_debugOutput.clear();
        if (m_debugger) m_debugger->takeBrowsed();
        bool interrupted = m_interruptRequested.exchange(false);
        m_executing = false;
        r::clearRInterrupt();

        SEXP silent_ = PROTECT(Rf_ScalarLogical(cell->silent));
        SEXP failed_ = PROTECT(Rf_ScalarLogical(cell->failed || interrupted));
        try { r::invokeHeraFn(".jv.repl.cell_done", silent_, failed_); }
        catch (const std::exception& e) { log::warning(std::string("finishing a cell: ") + e.what()); }
        UNPROTECT(2);
        SETCDR(Rf_install(".jv_cell"), R_NilValue);
        if (m_debugger && m_debugger->started()) {
            m_debugger->applyBreakpoints();
        }

        if (interrupted) {
            if (!cell->silent) publishExecutionError("KeyboardInterrupt", "", {});
            cell->reply(adrastea::createErrorReply("KeyboardInterrupt", "", {}));
        } else if (cell->failed && !cell->silent) {
            publishExecutionError("ERROR", cell->evalue, cell->traceback);
            cell->reply(adrastea::createErrorReply("ERROR", cell->evalue, cell->traceback));
        } else {
            // (a silent execution reports no error, as before)
            if (!cell->silent && !cell->failed && !cell->valueText.empty()) {
                std::string text = cell->valueText;
                if (text.back() == '\n') text.pop_back();
                publishExecutionResult(cell->executionCount, { { "text/plain", text } }, adrastea::json::object());
            }
            cell->reply(adrastea::createSuccessfulReply(adrastea::json::array(),
                cell->failed ? adrastea::json::object() : evalUserExpressions(cell->userExpressions)));
        }
    }

    adrastea::json RInterpreter::isCompleteRequestImpl(const std::string& code_)
    {
        SEXP code = PROTECT(Rf_mkString(withLineFeeds(code_).c_str()));

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
        SEXP result = PROTECT(r::invokeHeraFn(".elara.complete", code_, cursor_pos_));

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
        SEXP result = PROTECT(r::invokeHeraFn(".jv.inspect.request", code_, cursor_pos_));

        bool found = LOGICAL_ELT(VECTOR_ELT(result, 0), 0);
        if (!found) {
            UNPROTECT(3);
            return adrastea::createInspectReply(false);
        }

        auto data = routines::jsonFromR(VECTOR_ELT(result, 1));
        UNPROTECT(3);
        return adrastea::createInspectReply(found, data);
    }

    adrastea::json RInterpreter::shutdownRequestImpl(bool restart)
    {
        // R is not ended here: this runs inside R's console loop (readConsole()), which ends R itself once the
        // server has stopped (the end of its input), after the reply has gone out
        m_rEnded = true;
        return adrastea::createShutdownReply(restart);
    }

    void RInterpreter::idleImpl()
    {
        // what R's own console does while it waits for input: its events, its help server (see serviceREvents())
        if (!m_rEnded && m_server) r::serviceREvents();

        // R's own console runs the `later` event loop whenever R waits for
        // input; an embedded R never waits for input, so without this,
        // callbacks scheduled with later::later() -- and everything built on
        // them: promises, httpuv servers (Shiny, plumber) -- would only run
        // when something called later::run_now(). Running what is due while
        // the kernel is idle lets them progress in the background between
        // cells, as they do at RStudio's console. Output from the callbacks
        // goes to the latest request.
        //
        // Cheap when later is unused: a namespace lookup every idle slice.
        // R_tryEval() catches an error from a callback (R prints it, through
        // the console hook) so it cannot unwind past this frame.
        if (m_rEnded) return;
        if (!m_idleExpression) {
            SEXP code = PROTECT(Rf_mkString(
                "if (isNamespaceLoaded('later') && !later::loop_empty()) later::run_now(0)"));
            ParseStatus status;
            SEXP parsed = PROTECT(R_ParseVector(code, -1, &status, R_NilValue));
            if (status != PARSE_OK || Rf_length(parsed) < 1) {
                UNPROTECT(2);
                m_rEnded = true; // never retry a broken expression
                return;
            }
            SEXP expression = VECTOR_ELT(parsed, 0);
            R_PreserveObject(expression);
            m_idleExpression = expression;
            UNPROTECT(2);
        }
        int error_occurred = 0;
        R_tryEval(static_cast<SEXP>(m_idleExpression), R_GlobalEnv, &error_occurred);
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
            m_interruptRequested = true;
            r::requestRInterrupt();
#ifndef _WIN32
            // Setting the flag is enough for R code that keeps evaluating (a
            // loop), but a blocking call -- Sys.sleep() -- only looks at it
            // after the OS call returns. A signal to the R thread makes that
            // call return early (EINTR), exactly what Ctrl-C does in a
            // terminal. SIGUSR2 has a no-op handler (installed in the
            // constructor), so it can never terminate the process.
            pthread_kill(m_mainThread, SIGUSR2);
#endif
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

        auto info = adrastea::createInfoReply(
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
        info["debugger"] = true; // the Jupyter debug protocol: see debugRequestImpl()
        return info;
    }
}
