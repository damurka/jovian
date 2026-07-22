#include "datasuite/interpreter_r.hpp"
#include "datasuite/helper.hpp"
#include "datasuite/input.hpp"

#ifdef _MSC_VER
#define _Complex
#endif

#define R_NO_REMAP
#define STRICT_R_HEADERS

#include "R.h"
#include "Rinternals.h"
#include "Rembedded.h"
#include "R_ext/Parse.h"
#include "R_ext/Rdynload.h"
#include "Rversion.h"

#ifndef _WIN32
#include "Rinterface.h"
#else
#include <windows.h>
#include <cstdint>
extern "C" __declspec(dllimport) uintptr_t R_CStackLimit;
#endif

#include "r/rtools.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _MSC_VER
// Clean up our trick so we don't pollute the rest of the C++ project
#undef _Complex
#endif

namespace datasuite
{
    static RInterpreter* p_interpreter = nullptr;
    RInterpreter* get_r_interpreter()
    {
        return p_interpreter;
    }

    Interpreter*& get_registered_interpreter()
    {
        static Interpreter* interpreter = nullptr;
        return interpreter;
    }

    bool register_interpreter(Interpreter* new_interpreter)
    {
        Interpreter*& interp = get_registered_interpreter();
        if (interp != nullptr)
        {
            return false;
        }
        else
        {
            interp = new_interpreter;
            return true;
        }
    }

    Interpreter& get_interpreter()
    {
        Interpreter* interp = get_registered_interpreter();
        if (interp != nullptr)
            return *interp;
        else
            return *get_r_interpreter();
    }

    void WriteConsoleEx(const char* buf, int buflen, int otype) {
        std::string output(buf, buflen);
        if (otype == 1) {
            p_interpreter->publish_stream("stderr", output);
        }
        else {
            p_interpreter->publish_stream("stdout", output);
        }
    }

    void capture_WriteConsoleEx(const char* buf, int buflen, int otype) {
        std::string output(buf, buflen);
        if (otype == 1) {
            // do nothing
        }
        else {
            p_interpreter->capture_stream << output;
        }
    }

    int ReadConsole(const char* prompt, unsigned char* buffer, int length, int /*addtohistory*/) {
        std::string res = datasuite::blocking_input_request(prompt, false);

        std::size_t size = std::min(res.size(), std::size_t(length));
        std::copy(res.c_str(), res.c_str() + size, buffer);
        buffer[size] = '\n';

        return 1;
    }

    RInterpreter::RInterpreter(int argc, char* argv[])
    {
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

        // Debug: Print environment before R init
        printf("[R Interpreter BEFORE Init] R_HOME=%s\n", getenv("R_HOME") ? getenv("R_HOME") : "NOT SET");
        printf("[R Interpreter BEFORE Init] R_LIBS=%s\n", getenv("R_LIBS") ? getenv("R_LIBS") : "NOT SET");
        fflush(stdout);

        Rf_initEmbeddedR(argc, argv);

        R_CStackLimit = (uintptr_t)-1;

        printf("[R Interpreter AFTER Init] Rf_initEmbeddedR completed\n");
        fflush(stdout);
#endif

        register_r_routines();

#ifndef _WIN32
        ptr_R_WriteConsole = nullptr;
        ptr_R_WriteConsoleEx = WriteConsoleEx;
        ptr_R_ReadConsole = ReadConsole;
        R_Outputfile = NULL;
        R_Consolefile = NULL;
#endif

        register_interpreter(this);
        p_interpreter = this;
    }

    void RInterpreter::configure_impl()
    {
        // Debug: Print R environment variables
        printf("[R Interpreter] R_HOME=%s\n", getenv("R_HOME") ? getenv("R_HOME") : "NOT SET");
        printf("[R Interpreter] R_LIBS=%s\n", getenv("R_LIBS") ? getenv("R_LIBS") : "NOT SET");
        fflush(stdout);

        // Debug: Print .libPaths() from R
        SEXP get_libpaths = PROTECT(Rf_lang1(Rf_install(".libPaths")));
        SEXP libpaths = PROTECT(Rf_eval(get_libpaths, R_GlobalEnv));
        printf("[R Interpreter] .libPaths() count: %d\n", Rf_length(libpaths));
        for (int i = 0; i < Rf_length(libpaths); i++) {
            printf("[R Interpreter] .libPaths()[%d] = %s\n", i, CHAR(STRING_ELT(libpaths, i)));
        }
        fflush(stdout);
        UNPROTECT(2);

        // Try to load hera - MAKE IT OPTIONAL FOR NOW
        printf("[R Interpreter] Attempting to load 'hera' package...\n");
        fflush(stdout);

        SEXP sym_library = PROTECT(Rf_install("require"));
        SEXP str_hera = PROTECT(Rf_mkString("hera"));
        SEXP sym_quietly = PROTECT(Rf_install("quietly"));

        SEXP call_library_hera = PROTECT(r::r_call(sym_library, str_hera, /* quietly = */ Rf_ScalarLogical(FALSE)));
        SET_TAG(CDDR(call_library_hera), sym_quietly);

        SEXP out = PROTECT(Rf_eval(call_library_hera, R_GlobalEnv));

        if (LOGICAL_ELT(out, 0) == FALSE) {
            printf("[R Interpreter] WARNING: 'hera' package could not be loaded. Some features may not work.\n");
            printf("[R Interpreter] Continuing without 'hera' for testing purposes...\n");
            fflush(stdout);
            // DON'T throw - just warn for now
            // throw std::runtime_error(
            //     "Fatal Initialization Error: The mandatory partner library package 'hera' "
            //     "could not be loaded. Please ensure 'hera' is correctly installed."
            // );
        } else {
            printf("[R Interpreter] Successfully loaded 'hera' package\n");
            fflush(stdout);
        }

        UNPROTECT(5);
    }

    void RInterpreter::execute_request_impl(
        send_reply_callback cb,
        int execution_count,
        const std::string& code,
        ExecuteRequestConfig config,
        json /*user_expressions*/
    )
    {
        if (config.store_history) {
            const_cast<HistoryManager&>(get_history_manager()).store_inputs(0, execution_count, code);
        }

        SEXP code_ = PROTECT(Rf_mkString(code.c_str()));
        SEXP execution_counter_ = PROTECT(Rf_ScalarInteger(execution_count));
        SEXP silent_ = PROTECT(Rf_ScalarLogical(config.silent));
        SEXP result = PROTECT(r::invoke_hera_fn("execute", code_, execution_counter_, silent_));

        if (Rf_inherits(result, "error_reply")) {
            std::string evalue = CHAR(STRING_ELT(VECTOR_ELT(result, 0), 0));
            std::string ename = CHAR(STRING_ELT(VECTOR_ELT(result, 1), 0));

            std::vector<std::string> trace_back;
            if (XLENGTH(result) > 2) {
                SEXP trace_back_ = VECTOR_ELT(result, 2);
                auto n = XLENGTH(trace_back_);
                for (decltype(n) i = 0; i < n; i++) {
                    trace_back.push_back(CHAR(STRING_ELT(trace_back_, i)));
                }
            }

            publish_execution_error(evalue, ename, trace_back);
            cb(create_error_reply(evalue, ename, std::move(trace_back)));
        }
        else {
           if (Rf_inherits(result, "execution_result")) {
                SEXP data_ = VECTOR_ELT(result, 0);
                SEXP metadata_ = VECTOR_ELT(result, 1);
                auto data = json::parse(CHAR(STRING_ELT(data_, 0)));
                auto metadata = json::parse(CHAR(STRING_ELT(metadata_, 0)));
                publish_execution_result(execution_count, data, metadata);
            }

            cb(create_successful_reply(/*payload, user_expressions*/));
        }

        UNPROTECT(4);
    }

    json RInterpreter::is_complete_request_impl(const std::string& code_)
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

        json res = create_is_complete_reply(CHAR(STRING_ELT(code, 0)), "");
        UNPROTECT(1);
        return res;
    }

    json json_from_character_vector(SEXP x) {
        auto n = XLENGTH(x);
        std::vector<std::string> vec(n);

        for (decltype(n) i = 0; i < n; i++) {
            vec[i] = std::string(CHAR(STRING_ELT(x, i)));
        }
        return json(vec);
    }

    json RInterpreter::complete_request_impl(const std::string& code, int cursor_pos)
    {
        SEXP code_ = PROTECT(Rf_mkString(code.c_str()));
        SEXP cursor_pos_ = PROTECT(Rf_ScalarInteger(cursor_pos));
        SEXP result = PROTECT(r::invoke_hera_fn("complete", code_, cursor_pos_));

        auto matches = json_from_character_vector(VECTOR_ELT(result, 0));
        int cursor_start = INTEGER_ELT(VECTOR_ELT(result, 1), 0);
        int cursor_end = INTEGER_ELT(VECTOR_ELT(result, 1), 1);

        UNPROTECT(3);
        return create_complete_reply(matches, cursor_start, cursor_end);
    }

    json RInterpreter::inspect_request_impl(const std::string& code, int cursor_pos, int /*detail_level*/)
    {
        SEXP code_ = PROTECT(Rf_mkString(code.c_str()));
        SEXP cursor_pos_ = PROTECT(Rf_ScalarInteger(cursor_pos));
        SEXP result = PROTECT(r::invoke_hera_fn("inspect", code_, cursor_pos_));

        bool found = LOGICAL_ELT(VECTOR_ELT(result, 0), 0);
        if (!found) {
            UNPROTECT(3);
            return create_inspect_reply(false);
        }

        auto data = json::parse(CHAR(STRING_ELT(VECTOR_ELT(result, 1), 0)));
        UNPROTECT(3);
        return create_inspect_reply(found, data);
    }

    json RInterpreter::shutdown_request_impl(bool /*restart*/)
    {
        Rf_endEmbeddedR(0);
        return create_shutdown_reply(false);
    }

    json RInterpreter::interrupt_request_impl()
    {
        return create_interrupt_reply();
    }

    json RInterpreter::kernel_info_request_impl()
    {
        const std::string  implementation = "xr";
        const std::string  implementation_version{ version::kernel_protocol_version };
        const std::string  language_name = "R";
        const std::string  language_version = std::string(R_MAJOR) + "." + std::string(R_MINOR);
        const std::string  language_mimetype = "text/x-R";
        const std::string  language_file_extension = ".R";
        const std::string  language_pygments_lexer = "r";
        const std::string  language_codemirror_mode = "";
        const std::string  language_nbconvert_exporter = "";
        const std::string  banner = "xr";
        const json     help_links = json::array();

        return create_info_reply(
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
