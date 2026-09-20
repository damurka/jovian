#include "elara/r/r_dynlib.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdlib>
#include <stdexcept>

namespace elara { namespace r {

namespace api {
    Rf_initEmbeddedR_t p_Rf_initEmbeddedR = nullptr;
    Rf_endEmbeddedR_t p_Rf_endEmbeddedR = nullptr;
    Rf_cons_t p_Rf_cons = nullptr;
    Rf_lcons_t p_Rf_lcons = nullptr;
    Rf_install_t p_Rf_install = nullptr;
    Rf_mkString_t p_Rf_mkString = nullptr;
    Rf_mkChar_t p_Rf_mkChar = nullptr;
    Rf_eval_t p_Rf_eval = nullptr;
    R_tryEval_t p_R_tryEval = nullptr;
    R_curErrorBuf_t p_R_curErrorBuf = nullptr;
    Rf_classgets_t p_Rf_classgets = nullptr;
    Rf_namesgets_t p_Rf_namesgets = nullptr;
    Rf_allocVector_t p_Rf_allocVector = nullptr;
    Rf_xlength_t p_Rf_xlength = nullptr;
    XLENGTH_t p_XLENGTH = nullptr;
    Rf_isNull_t p_Rf_isNull = nullptr;
    Rf_isString_t p_Rf_isString = nullptr;
    Rf_length_t p_Rf_length = nullptr;
    Rf_lang1_t p_Rf_lang1 = nullptr;
    Rf_ScalarInteger_t p_Rf_ScalarInteger = nullptr;
    Rf_ScalarLogical_t p_Rf_ScalarLogical = nullptr;
    Rf_inherits_t p_Rf_inherits = nullptr;
    R_ParseVector_t p_R_ParseVector = nullptr;
    R_MakeExternalPtr_t p_R_MakeExternalPtr = nullptr;
    R_RegisterCFinalizerEx_t p_R_RegisterCFinalizerEx = nullptr;
    R_ExternalPtrAddr_t p_R_ExternalPtrAddr = nullptr;
    R_registerRoutines_t p_R_registerRoutines = nullptr;
    R_getEmbeddingDllInfo_t p_R_getEmbeddingDllInfo = nullptr;
    R_tryCatchError_t p_R_tryCatchError = nullptr;
    STRING_ELT_t p_STRING_ELT = nullptr;
    SET_STRING_ELT_t p_SET_STRING_ELT = nullptr;
    R_CHAR_t p_R_CHAR = nullptr;
    RAW_t p_RAW = nullptr;
    VECTOR_ELT_t p_VECTOR_ELT = nullptr;
    SET_VECTOR_ELT_t p_SET_VECTOR_ELT = nullptr;
    LOGICAL_ELT_t p_LOGICAL_ELT = nullptr;
    INTEGER_ELT_t p_INTEGER_ELT = nullptr;
    Rf_protect_t p_Rf_protect = nullptr;
    Rf_unprotect_t p_Rf_unprotect = nullptr;

    SEXP* p_R_GlobalEnv = nullptr;
    SEXP* p_R_NilValue = nullptr;

#ifndef _WIN32
    ptr_R_WriteConsole_t* p_ptr_R_WriteConsole = nullptr;
    ptr_R_WriteConsoleEx_t* p_ptr_R_WriteConsoleEx = nullptr;
    ptr_R_ReadConsole_t* p_ptr_R_ReadConsole = nullptr;
    FILE** p_R_Outputfile = nullptr;
    FILE** p_R_Consolefile = nullptr;
    int* p_R_Interactive = nullptr;
#else
    R_setStartTime_t p_R_setStartTime = nullptr;
    R_DefParamsEx_t p_R_DefParamsEx = nullptr;
    R_common_command_line_t p_R_common_command_line = nullptr;
    R_SetParams_t p_R_SetParams = nullptr;
    R_set_command_line_arguments_t p_R_set_command_line_arguments = nullptr;
    setup_Rmainloop_t p_setup_Rmainloop = nullptr;
    get_R_HOME_t p_get_R_HOME = nullptr;
    getRUser_t p_getRUser = nullptr;
#endif
}

namespace {

    bool g_loaded = false;

#ifdef _WIN32
    bool g_hasWindowsEmbeddingApi = false;
#endif

#ifdef _WIN32
    using LibHandle = HMODULE;

    std::string formatLastError(DWORD code) {
        char* buf = nullptr;
        DWORD len = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<char*>(&buf), 0, nullptr);
        std::string message = (len > 0 && buf) ? std::string(buf, len) : "unknown error";
        if (buf) LocalFree(buf);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
        return message;
    }

    // Deliberately just "R.dll", not a reconstructed full path: by the
    // time this runs, elara::Server::setupEnvironment() has already
    // prepended the configured R bin directory to this process's PATH
    // (engine.cpp), so ordinary Windows DLL search order finds it exactly
    // the way implicit/static linking would have -- and finds Rblas.dll/
    // Rlapack.dll as R.dll's own transitive dependencies the same way.
    LibHandle openRLibrary(std::string& outPath) {
        outPath = "R.dll";
        return ::LoadLibraryA("R.dll");
    }

    std::string describeOpenFailure(const std::string& path) {
        DWORD err = ::GetLastError();
        const char* r_home = std::getenv("R_HOME");
        return "Could not load " + path + " (" + formatLastError(err) + "). Is R installed? "
               "Checked PATH and R_HOME=" + std::string(r_home ? r_home : "(not set)") +
               ". Install R from https://cran.r-project.org, or make sure R_HOME/the R bin "
               "directory is configured correctly.";
    }

    void* getSym(LibHandle handle, const char* name) {
        return reinterpret_cast<void*>(::GetProcAddress(handle, name));
    }
#else
    using LibHandle = void*;

    // Unlike Windows' PATH-based DLL search, dlopen() does NOT consult
    // $PATH -- it only looks at LD_LIBRARY_PATH (Linux) / DYLD_LIBRARY_PATH
    // (macOS) and the system default library directories, neither of which
    // elara::Server::setupEnvironment() sets (it only prepends to PATH).
    // So unlike Windows' bare "R.dll", the full path has to be constructed
    // explicitly from R_HOME here -- this matches Positron's Ark (its libr
    // crate resolves R_HOME first, then dlopen()s libR.so/libR.dylib
    // directly under it), and is also just the standard, documented
    // location `R CMD config --ldflags` itself reports (-L$R_HOME/lib -lR).
    LibHandle openRLibrary(std::string& outPath) {
        const char* r_home = std::getenv("R_HOME");
        if (!r_home || !*r_home) {
            outPath.clear();
            return nullptr;
        }
#ifdef __APPLE__
        outPath = std::string(r_home) + "/lib/libR.dylib";
#else
        outPath = std::string(r_home) + "/lib/libR.so";
#endif
        // RTLD_GLOBAL (not RTLD_LOCAL) is required, not just preferred: R
        // packages loaded later via dyn.load()/.Call() are themselves
        // shared objects that reference libR's symbols (Rf_error, SEXP
        // accessors, ...) without linking against libR directly -- they
        // depend on those symbols already being visible process-wide from
        // when the R interpreter itself loaded libR. RTLD_LOCAL would make
        // every compiled R package fail to load with unresolved symbols.
        return ::dlopen(outPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    }

    std::string describeOpenFailure(const std::string& path) {
        const char* r_home = std::getenv("R_HOME");
        // dlerror() must be called exactly once here, not twice: POSIX
        // clears the recorded error as soon as it's returned, so a second
        // call (as this used to do, once in a condition and again inside
        // its branch) always sees NULL by then -- std::string(nullptr) is
        // undefined behavior, and this is confirmed to be exactly what
        // crashed a real CI run (a segfault in ElaraTest on macOS).
        const char* dlerror_msg = ::dlerror();
        std::string reason = dlerror_msg ? std::string(dlerror_msg) : "unknown error";
        std::string message;
        if (!r_home || !*r_home) {
            message = "R_HOME is not set -- elara needs a working R installation to run. "
                      "Please install R from https://cran.r-project.org and make sure R_HOME "
                      "is set, or configure the R installation path in settings.";
        } else {
            message = "Could not load " + path + " (" + reason + "). Is R installed at '" +
                       std::string(r_home) + "'?";
#ifndef __APPLE__
            message += " If this R was built from source, it needs to have been configured "
                       "with --enable-R-shlib, or no libR.so exists at all (only a static libR.a "
                       "baked into the R executable) -- the same requirement Positron's Ark documents "
                       "for this exact situation.";
#endif
        }
        return message;
    }

    void* getSym(LibHandle handle, const char* name) {
        return ::dlsym(handle, name);
    }
#endif

    template <class FnPtr>
    void resolve(LibHandle handle, const char* name, FnPtr& out, const std::string& libPath) {
        void* sym = getSym(handle, name);
        if (!sym) {
            throw std::runtime_error(
                "'" + libPath + "' was loaded but is missing the expected symbol '" + name +
                "' -- this usually means an incompatible or corrupted R installation. "
                "Try reinstalling R from https://cran.r-project.org.");
        }
        out = reinterpret_cast<FnPtr>(sym);
    }

    // Unlike resolve(), a missing symbol is not an error -- reports whether it
    // was found (and leaves `out` null if not). Only for symbols that don't
    // exist in every R version this loader supports.
    template <class FnPtr>
    bool tryResolve(LibHandle handle, const char* name, FnPtr& out) {
        void* sym = getSym(handle, name);
        out = reinterpret_cast<FnPtr>(sym);
        return sym != nullptr;
    }

    template <class T>
    void resolveData(LibHandle handle, const char* name, T*& out, const std::string& libPath) {
        void* sym = getSym(handle, name);
        if (!sym) {
            throw std::runtime_error(
                "'" + libPath + "' was loaded but is missing the expected symbol '" + name +
                "' -- this usually means an incompatible or corrupted R installation. "
                "Try reinstalling R from https://cran.r-project.org.");
        }
        out = reinterpret_cast<T*>(sym);
    }

} // namespace

bool isRApiLoaded() { return g_loaded; }

#ifdef _WIN32
bool hasWindowsEmbeddingApi() { return g_hasWindowsEmbeddingApi; }
#endif

void loadRApi() {
    if (g_loaded) return;

    std::string libPath;
    LibHandle handle = openRLibrary(libPath);
    if (!handle) {
        throw std::runtime_error(describeOpenFailure(libPath.empty() ? "R's shared library" : libPath));
    }

    using namespace api;
    resolve(handle, "Rf_initEmbeddedR", p_Rf_initEmbeddedR, libPath);
    resolve(handle, "Rf_endEmbeddedR", p_Rf_endEmbeddedR, libPath);
    resolve(handle, "Rf_cons", p_Rf_cons, libPath);
    resolve(handle, "Rf_lcons", p_Rf_lcons, libPath);
    resolve(handle, "Rf_install", p_Rf_install, libPath);
    resolve(handle, "Rf_mkString", p_Rf_mkString, libPath);
    resolve(handle, "Rf_mkChar", p_Rf_mkChar, libPath);
    resolve(handle, "Rf_eval", p_Rf_eval, libPath);
    resolve(handle, "R_tryEval", p_R_tryEval, libPath);
    resolve(handle, "R_curErrorBuf", p_R_curErrorBuf, libPath);
    resolve(handle, "Rf_classgets", p_Rf_classgets, libPath);
    resolve(handle, "Rf_namesgets", p_Rf_namesgets, libPath);
    resolve(handle, "Rf_allocVector", p_Rf_allocVector, libPath);
    resolve(handle, "Rf_xlength", p_Rf_xlength, libPath);
    resolve(handle, "XLENGTH", p_XLENGTH, libPath);
    resolve(handle, "Rf_isNull", p_Rf_isNull, libPath);
    resolve(handle, "Rf_isString", p_Rf_isString, libPath);
    resolve(handle, "Rf_length", p_Rf_length, libPath);
    resolve(handle, "Rf_lang1", p_Rf_lang1, libPath);
    resolve(handle, "Rf_ScalarInteger", p_Rf_ScalarInteger, libPath);
    resolve(handle, "Rf_ScalarLogical", p_Rf_ScalarLogical, libPath);
    resolve(handle, "Rf_inherits", p_Rf_inherits, libPath);
    resolve(handle, "R_ParseVector", p_R_ParseVector, libPath);
    resolve(handle, "R_MakeExternalPtr", p_R_MakeExternalPtr, libPath);
    resolve(handle, "R_RegisterCFinalizerEx", p_R_RegisterCFinalizerEx, libPath);
    resolve(handle, "R_ExternalPtrAddr", p_R_ExternalPtrAddr, libPath);
    resolve(handle, "R_registerRoutines", p_R_registerRoutines, libPath);
    resolve(handle, "R_getEmbeddingDllInfo", p_R_getEmbeddingDllInfo, libPath);
    resolve(handle, "R_tryCatchError", p_R_tryCatchError, libPath);
    resolve(handle, "STRING_ELT", p_STRING_ELT, libPath);
    resolve(handle, "SET_STRING_ELT", p_SET_STRING_ELT, libPath);
    resolve(handle, "R_CHAR", p_R_CHAR, libPath);
    resolve(handle, "RAW", p_RAW, libPath);
    resolve(handle, "VECTOR_ELT", p_VECTOR_ELT, libPath);
    resolve(handle, "SET_VECTOR_ELT", p_SET_VECTOR_ELT, libPath);
    resolve(handle, "LOGICAL_ELT", p_LOGICAL_ELT, libPath);
    resolve(handle, "INTEGER_ELT", p_INTEGER_ELT, libPath);
    resolve(handle, "Rf_protect", p_Rf_protect, libPath);
    resolve(handle, "Rf_unprotect", p_Rf_unprotect, libPath);

    resolveData(handle, "R_GlobalEnv", p_R_GlobalEnv, libPath);
    resolveData(handle, "R_NilValue", p_R_NilValue, libPath);

#ifndef _WIN32
    resolveData(handle, "ptr_R_WriteConsole", p_ptr_R_WriteConsole, libPath);
    resolveData(handle, "ptr_R_WriteConsoleEx", p_ptr_R_WriteConsoleEx, libPath);
    resolveData(handle, "ptr_R_ReadConsole", p_ptr_R_ReadConsole, libPath);
    resolveData(handle, "R_Outputfile", p_R_Outputfile, libPath);
    resolveData(handle, "R_Consolefile", p_R_Consolefile, libPath);
    // Lenient on purpose (a plain dlsym, not resolveData()): if some libR
    // doesn't export it, readline() just keeps its old behavior rather than
    // R failing to start over a setting that only matters for stdin.
    p_R_Interactive = static_cast<int*>(getSym(handle, "R_Interactive"));
#else
    // Optional, not resolve(): R_DefParamsEx (the versioned-Rstart entry
    // point) only exists from R 4.2.0. All-or-nothing -- a partial set is no
    // better than none, since the whole documented sequence is needed
    // together (see RInterpreter's Windows startup).
    g_hasWindowsEmbeddingApi =
        tryResolve(handle, "R_setStartTime", p_R_setStartTime) &&
        tryResolve(handle, "R_DefParamsEx", p_R_DefParamsEx) &&
        tryResolve(handle, "R_common_command_line", p_R_common_command_line) &&
        tryResolve(handle, "R_SetParams", p_R_SetParams) &&
        tryResolve(handle, "R_set_command_line_arguments", p_R_set_command_line_arguments) &&
        tryResolve(handle, "setup_Rmainloop", p_setup_Rmainloop) &&
        tryResolve(handle, "get_R_HOME", p_get_R_HOME) &&
        tryResolve(handle, "getRUser", p_getRUser);
#endif

    g_loaded = true;
}

} }
