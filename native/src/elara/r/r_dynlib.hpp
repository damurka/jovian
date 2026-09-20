#ifndef ELARA_R_DYNLIB_HPP
#define ELARA_R_DYNLIB_HPP

// Single include point for R's C API across this codebase.
//
// elara loads R's shared library (R.dll / libR.so / libR.dylib) at RUNTIME
// via LoadLibrary+GetProcAddress (Windows) or dlopen+dlsym (Linux/macOS)
// instead of linking against it at build time (Windows: LoadLibrary(R.dll);
// Linux/macOS: dlopen(libR.so/libR.dylib)), for two reasons:
//
//   1. Implicit/static linking means the OS process loader resolves R's
//      shared library *before* elara.exe's own main() ever runs. If it
//      can't be found there, the process simply fails to start, with no
//      chance for our own code to detect that and report it cleanly.
//      elara.exe is normally launched headlessly by a supervisor process
//      (themisto) or a VS Code fork's extension host, neither of which
//      currently checks whether R is installed before spawning it --
//      loadRApi() below turns "R is missing" into an ordinary, catchable
//      std::runtime_error with an actionable message instead.
//   2. It decouples elara from any specific R version baked in at build
//      time: which R gets loaded is entirely a runtime decision (R_HOME),
//      so switching R installations (e.g. R 4.4 -> R 4.6, via rig/conda/
//      a different rHome on session create/restart -- see
//      SessionRegistry::restartSession()) needs no rebuild/relink.
//
// On Linux, a source-built R needs `--enable-R-shlib` at configure time or
// no libR.so exists at all (only a static libR.a baked into the R
// executable) -- loadRApi() fails with an actionable message pointing at
// that.

#define R_NO_REMAP
#ifdef _MSC_VER
#define _Complex
#endif

#include "R.h"
#include "Rinternals.h"
#include "Rembedded.h"
#include "R_ext/Parse.h"
#include "R_ext/Rdynload.h"

#ifndef _WIN32
// Rinterface.h guards its ptr_R_WriteConsole/ptr_R_WriteConsoleEx/
// ptr_R_ReadConsole extern declarations behind R_INTERFACE_PTRS -- included
// here, before the macro section below exists, deliberately: defining
// those names as macros first and including this header afterward would
// macro-substitute Rinterface.h's OWN declaration lines (not just call
// sites), corrupting them. Every R header this file cares about has to be
// fully parsed before any of these macros are defined, for the same reason.
#define R_INTERFACE_PTRS 1
#include "Rinterface.h"
#endif

#ifdef _WIN32
// Windows R's documented embedding API (R_ext/RStartup.h, "Writing R
// Extensions" section 8.2.2). structRstart's Windows-only fields
// (ReadConsole/WriteConsoleEx/CharacterMode/...) are only visible under
// `Win32`, which R's own build defines but an embedder has to define itself
// -- R's own rtest.c example does exactly this. Included here, before the
// macro section below, for the same reason Rinterface.h is above.
#define Win32
#include "R_ext/RStartup.h"
#undef Win32
#endif

#ifdef _MSC_VER
#undef _Complex
#endif

#include <cstdio>
#include <string>

namespace elara { namespace r {

    // Loads R's shared library and resolves every symbol this codebase
    // calls (see r_dynlib.cpp for the full list). Throws
    // std::runtime_error with a specific, actionable message if the
    // library or any expected symbol can't be found. Safe to call more
    // than once -- only the first call does any work.
    //
    // Must be called before touching ANY of the R API names below (they
    // are null function pointers until this succeeds).
    void loadRApi();

    bool isRApiLoaded();

    // Asks R to break out of whatever it is evaluating, exactly as Ctrl-C
    // would: sets the flag R's evaluator polls (R_interrupts_pending on
    // Unix, UserBreak on Windows) -- R notices at its next
    // R_CheckUserInterrupt() and unwinds to top level with an "interrupt"
    // condition. A plain int store, so it is safe to call from any thread
    // (that is the whole point: the kernel's control-channel thread calls it
    // while the R thread is busy). Returns false if this R doesn't export
    // the flag.
    bool requestRInterrupt();

#ifdef _WIN32
    // Whether R.dll exports everything RInterpreter's Windows startup needs
    // (R_DefParamsEx and friends, all resolved by loadRApi() but -- unlike
    // every other symbol -- optionally, since R_DefParamsEx only exists from
    // R 4.2.0). False means an older R: the caller falls back to plain
    // Rf_initEmbeddedR(), which works but has no way to install a
    // ReadConsole callback, so R's readline()/scan() can't be answered.
    bool hasWindowsEmbeddingApi();
#endif

} }

extern "C" {
    using Rf_initEmbeddedR_t = int (*)(int, char**);
    using Rf_endEmbeddedR_t = void (*)(int);
    using Rf_cons_t = SEXP (*)(SEXP, SEXP);
    using Rf_lcons_t = SEXP (*)(SEXP, SEXP);
    using Rf_install_t = SEXP (*)(const char*);
    using Rf_mkString_t = SEXP (*)(const char*);
    using Rf_mkChar_t = SEXP (*)(const char*);
    using Rf_eval_t = SEXP (*)(SEXP, SEXP);
    using R_tryEval_t = SEXP (*)(SEXP, SEXP, int*);
    using R_curErrorBuf_t = const char* (*)(void);
    using Rf_classgets_t = SEXP (*)(SEXP, SEXP);
    using Rf_namesgets_t = SEXP (*)(SEXP, SEXP);
    using Rf_allocVector_t = SEXP (*)(SEXPTYPE, R_xlen_t);
    using Rf_xlength_t = R_xlen_t (*)(SEXP);
    using XLENGTH_t = R_xlen_t (*)(SEXP);
    using Rf_isNull_t = Rboolean (*)(SEXP);
    using Rf_isString_t = Rboolean (*)(SEXP);
    using Rf_length_t = R_len_t (*)(SEXP);
    using Rf_lang1_t = SEXP (*)(SEXP);
    using Rf_ScalarInteger_t = SEXP (*)(int);
    using Rf_ScalarLogical_t = SEXP (*)(int);
    using Rf_inherits_t = Rboolean (*)(SEXP, const char*);
    using R_ParseVector_t = SEXP (*)(SEXP, int, ParseStatus*, SEXP);
    using R_MakeExternalPtr_t = SEXP (*)(void*, SEXP, SEXP);
    using R_RegisterCFinalizerEx_t = void (*)(SEXP, R_CFinalizer_t, Rboolean);
    using R_ExternalPtrAddr_t = void* (*)(SEXP);
    using R_registerRoutines_t = int (*)(DllInfo*, const R_CMethodDef* const, const R_CallMethodDef* const,
                                          const R_FortranMethodDef* const, const R_ExternalMethodDef* const);
    using R_getEmbeddingDllInfo_t = DllInfo* (*)(void);
    using R_tryCatchError_t = SEXP (*)(SEXP (*)(void*), void*, SEXP (*)(SEXP, void*), void*);
    using STRING_ELT_t = SEXP (*)(SEXP, R_xlen_t);
    using SET_STRING_ELT_t = void (*)(SEXP, R_xlen_t, SEXP);
    using R_CHAR_t = const char* (*)(SEXP);
    using RAW_t = Rbyte* (*)(SEXP);
    using VECTOR_ELT_t = SEXP (*)(SEXP, R_xlen_t);
    using SET_VECTOR_ELT_t = SEXP (*)(SEXP, R_xlen_t, SEXP);
    using LOGICAL_ELT_t = int (*)(SEXP, R_xlen_t);
    using INTEGER_ELT_t = int (*)(SEXP, R_xlen_t);
    using Rf_protect_t = SEXP (*)(SEXP);
    using Rf_unprotect_t = void (*)(int);
}

#ifndef _WIN32
// Only used by interpreter_r.cpp's non-Windows console-hook wiring
// (#ifndef _WIN32 block, guarded there by R_INTERFACE_PTRS/Rinterface.h).
// These are DATA symbols -- exported global variables holding a function
// pointer (or, for R_Outputfile/R_Consolefile, a FILE*) -- not functions
// themselves, so loadRApi() resolves the address of the variable and we
// read/write through it, same as R_GlobalEnv/R_NilValue below. Signatures
// verified against R's actual source (src/include/Rinterface.h), not
// guessed -- an ABI mismatch here would be exactly the kind of
// hard-to-debug failure dynamic loading exists to avoid.
//
// Genuinely Unix-only, not just header-gated: R.dll on Windows does NOT
// export "ptr_R_WriteConsole"/"ptr_R_ReadConsole" at all (loadRApi() fails
// with "missing the expected symbol 'ptr_R_WriteConsole'" if you try --
// confirmed directly, and by inspecting R.dll's export table: R_ReadConsole/
// R_WriteConsole(Ex) exist there, but as plain functions, not assignable
// pointers). Windows R hooks the same callbacks through the documented
// Rstart startup sequence instead (R_DefParamsEx/R_SetParams with
// Rp->ReadConsole; see the api::p_R_DefParamsEx block below and
// RInterpreter's initEmbeddedRWindows()). Without that, R fell back to its
// own terminal I/O -- the hidden AllocConsole() window -- so readline()/
// scan() blocked forever with nothing able to answer them.
extern "C" {
    using ptr_R_WriteConsole_t = void (*)(const char*, int);
    using ptr_R_WriteConsoleEx_t = void (*)(const char*, int, int);
    using ptr_R_ReadConsole_t = int (*)(const char*, unsigned char*, int, int);
}
#endif

#ifdef _WIN32
// Windows-only counterpart to the ptr_R_* block above: R.dll does not export
// those (confirmed -- see interpreter_r.cpp's constructor), so console I/O
// is hooked through the documented Rstart callbacks instead. All plain
// functions (verified: every one lives in R.dll's .text section, none are
// data), called only through api::p_* below, never macro-redirected -- their
// names are also declared by RStartup.h itself.
extern "C" {
    using R_setStartTime_t = void (*)(void);
    using R_DefParamsEx_t = int (*)(Rstart, int);
    using R_common_command_line_t = void (*)(int*, char**, Rstart);
    using R_SetParams_t = void (*)(Rstart);
    using R_set_command_line_arguments_t = void (*)(int, char**);
    using setup_Rmainloop_t = void (*)(void);
    using get_R_HOME_t = char* (*)(void);
    using getRUser_t = char* (*)(void);
}
#endif

// One function pointer per R C API symbol this codebase calls, resolved
// via GetProcAddress/dlsym in loadRApi() (r_dynlib.cpp) rather than by the
// linker. The macros further down redirect ordinary call syntax (e.g.
// Rf_cons(a, b)) through these pointers; R's own macros that expand to
// these names (PROTECT -> Rf_protect, CHAR -> R_CHAR, XLENGTH ->
// Rf_xlength, ...) pick this up automatically since the preprocessor
// rescans expanded text for further macro substitution.
namespace elara { namespace r { namespace api {
    extern Rf_initEmbeddedR_t p_Rf_initEmbeddedR;
    extern Rf_endEmbeddedR_t p_Rf_endEmbeddedR;
    extern Rf_cons_t p_Rf_cons;
    extern Rf_lcons_t p_Rf_lcons;
    extern Rf_install_t p_Rf_install;
    extern Rf_mkString_t p_Rf_mkString;
    extern Rf_mkChar_t p_Rf_mkChar;
    extern Rf_eval_t p_Rf_eval;
    extern R_tryEval_t p_R_tryEval;
    extern R_curErrorBuf_t p_R_curErrorBuf;
    extern Rf_classgets_t p_Rf_classgets;
    extern Rf_namesgets_t p_Rf_namesgets;
    extern Rf_allocVector_t p_Rf_allocVector;
    extern Rf_xlength_t p_Rf_xlength;
    extern XLENGTH_t p_XLENGTH;
    extern Rf_isNull_t p_Rf_isNull;
    extern Rf_isString_t p_Rf_isString;
    extern Rf_length_t p_Rf_length;
    extern Rf_lang1_t p_Rf_lang1;
    extern Rf_ScalarInteger_t p_Rf_ScalarInteger;
    extern Rf_ScalarLogical_t p_Rf_ScalarLogical;
    extern Rf_inherits_t p_Rf_inherits;
    extern R_ParseVector_t p_R_ParseVector;
    extern R_MakeExternalPtr_t p_R_MakeExternalPtr;
    extern R_RegisterCFinalizerEx_t p_R_RegisterCFinalizerEx;
    extern R_ExternalPtrAddr_t p_R_ExternalPtrAddr;
    extern R_registerRoutines_t p_R_registerRoutines;
    extern R_getEmbeddingDllInfo_t p_R_getEmbeddingDllInfo;
    extern R_tryCatchError_t p_R_tryCatchError;
    extern STRING_ELT_t p_STRING_ELT;
    extern SET_STRING_ELT_t p_SET_STRING_ELT;
    extern R_CHAR_t p_R_CHAR;
    extern RAW_t p_RAW;
    extern VECTOR_ELT_t p_VECTOR_ELT;
    extern SET_VECTOR_ELT_t p_SET_VECTOR_ELT;
    extern LOGICAL_ELT_t p_LOGICAL_ELT;
    extern INTEGER_ELT_t p_INTEGER_ELT;
    extern Rf_protect_t p_Rf_protect;
    extern Rf_unprotect_t p_Rf_unprotect;

    extern SEXP* p_R_GlobalEnv;
    extern SEXP* p_R_NilValue;
    // R_interrupts_pending (Unix) / UserBreak (Windows); resolved
    // leniently, may be null. Use requestRInterrupt() rather than this.
    extern int* p_interruptFlag;

#ifndef _WIN32
    extern ptr_R_WriteConsole_t* p_ptr_R_WriteConsole;
    extern ptr_R_WriteConsoleEx_t* p_ptr_R_WriteConsoleEx;
    extern ptr_R_ReadConsole_t* p_ptr_R_ReadConsole;
    extern FILE** p_R_Outputfile;
    extern FILE** p_R_Consolefile;
    // Rboolean R_Interactive -- an int-sized enum. Resolved leniently (may
    // be null), unlike the pointers above: see RInterpreter's constructor.
    extern int* p_R_Interactive;
#else
    extern R_setStartTime_t p_R_setStartTime;
    extern R_DefParamsEx_t p_R_DefParamsEx;
    extern R_common_command_line_t p_R_common_command_line;
    extern R_SetParams_t p_R_SetParams;
    extern R_set_command_line_arguments_t p_R_set_command_line_arguments;
    extern setup_Rmainloop_t p_setup_Rmainloop;
    extern get_R_HOME_t p_get_R_HOME;
    extern getRUser_t p_getRUser;
#endif
} } }

#define Rf_initEmbeddedR (*::elara::r::api::p_Rf_initEmbeddedR)
#define Rf_endEmbeddedR (*::elara::r::api::p_Rf_endEmbeddedR)
#define Rf_cons (*::elara::r::api::p_Rf_cons)
#define Rf_lcons (*::elara::r::api::p_Rf_lcons)
#define Rf_install (*::elara::r::api::p_Rf_install)
#define Rf_mkString (*::elara::r::api::p_Rf_mkString)
#define Rf_mkChar (*::elara::r::api::p_Rf_mkChar)
#define Rf_eval (*::elara::r::api::p_Rf_eval)
#define R_tryEval (*::elara::r::api::p_R_tryEval)
#define R_curErrorBuf (*::elara::r::api::p_R_curErrorBuf)
#define Rf_classgets (*::elara::r::api::p_Rf_classgets)
#define Rf_namesgets (*::elara::r::api::p_Rf_namesgets)
#define Rf_allocVector (*::elara::r::api::p_Rf_allocVector)
#define Rf_xlength (*::elara::r::api::p_Rf_xlength)
#define XLENGTH (*::elara::r::api::p_XLENGTH)
#define Rf_isNull (*::elara::r::api::p_Rf_isNull)
#define Rf_isString (*::elara::r::api::p_Rf_isString)
#define Rf_length (*::elara::r::api::p_Rf_length)
#define Rf_lang1 (*::elara::r::api::p_Rf_lang1)
#define Rf_ScalarInteger (*::elara::r::api::p_Rf_ScalarInteger)
#define Rf_ScalarLogical (*::elara::r::api::p_Rf_ScalarLogical)
#define Rf_inherits (*::elara::r::api::p_Rf_inherits)
#define R_ParseVector (*::elara::r::api::p_R_ParseVector)
#define R_MakeExternalPtr (*::elara::r::api::p_R_MakeExternalPtr)
#define R_RegisterCFinalizerEx (*::elara::r::api::p_R_RegisterCFinalizerEx)
#define R_ExternalPtrAddr (*::elara::r::api::p_R_ExternalPtrAddr)
#define R_registerRoutines (*::elara::r::api::p_R_registerRoutines)
#define R_getEmbeddingDllInfo (*::elara::r::api::p_R_getEmbeddingDllInfo)
#define R_tryCatchError (*::elara::r::api::p_R_tryCatchError)
#define STRING_ELT (*::elara::r::api::p_STRING_ELT)
#define SET_STRING_ELT (*::elara::r::api::p_SET_STRING_ELT)
#define R_CHAR (*::elara::r::api::p_R_CHAR)
#define RAW (*::elara::r::api::p_RAW)
#define VECTOR_ELT (*::elara::r::api::p_VECTOR_ELT)
#define SET_VECTOR_ELT (*::elara::r::api::p_SET_VECTOR_ELT)
#define LOGICAL_ELT (*::elara::r::api::p_LOGICAL_ELT)
#define INTEGER_ELT (*::elara::r::api::p_INTEGER_ELT)
#define Rf_protect (*::elara::r::api::p_Rf_protect)
#define Rf_unprotect (*::elara::r::api::p_Rf_unprotect)

#define R_GlobalEnv (*::elara::r::api::p_R_GlobalEnv)
#define R_NilValue (*::elara::r::api::p_R_NilValue)

#ifndef _WIN32
#define ptr_R_WriteConsole (*::elara::r::api::p_ptr_R_WriteConsole)
#define ptr_R_WriteConsoleEx (*::elara::r::api::p_ptr_R_WriteConsoleEx)
#define ptr_R_ReadConsole (*::elara::r::api::p_ptr_R_ReadConsole)
#define R_Outputfile (*::elara::r::api::p_R_Outputfile)
#define R_Consolefile (*::elara::r::api::p_R_Consolefile)
#endif

#endif // ELARA_R_DYNLIB_HPP
