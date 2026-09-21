#include "carpo/py/py_dynlib.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace carpo { namespace py {

namespace api {
    Py_Initialize_t p_Py_Initialize = nullptr;
    Py_IsInitialized_t p_Py_IsInitialized = nullptr;
    Py_FinalizeEx_t p_Py_FinalizeEx = nullptr;

    PyErr_Occurred_t p_PyErr_Occurred = nullptr;
    PyErr_Fetch_t p_PyErr_Fetch = nullptr;
    PyErr_NormalizeException_t p_PyErr_NormalizeException = nullptr;
    PyErr_Clear_t p_PyErr_Clear = nullptr;
    PyErr_SetString_t p_PyErr_SetString = nullptr;

    PyObject_Str_t p_PyObject_Str = nullptr;
    PyObject_GetAttrString_t p_PyObject_GetAttrString = nullptr;
    PyObject_CallObject_t p_PyObject_CallObject = nullptr;

    Py_IncRef_t p_Py_IncRef = nullptr;
    Py_DecRef_t p_Py_DecRef = nullptr;

    PyUnicode_FromString_t p_PyUnicode_FromString = nullptr;
    PyUnicode_AsUTF8String_t p_PyUnicode_AsUTF8String = nullptr;
    PyBytes_AsString_t p_PyBytes_AsString = nullptr;

    PyTuple_New_t p_PyTuple_New = nullptr;
    PyTuple_SetItem_t p_PyTuple_SetItem = nullptr;
    PyTuple_GetItem_t p_PyTuple_GetItem = nullptr;

    PyDict_New_t p_PyDict_New = nullptr;
    PyDict_GetItemString_t p_PyDict_GetItemString = nullptr;
    PyDict_SetItemString_t p_PyDict_SetItemString = nullptr;

    PyList_Size_t p_PyList_Size = nullptr;
    PyList_GetItem_t p_PyList_GetItem = nullptr;

    PyLong_AsLong_t p_PyLong_AsLong = nullptr;
    PyLong_FromLong_t p_PyLong_FromLong = nullptr;

    PyImport_AddModule_t p_PyImport_AddModule = nullptr;
    PyModule_GetDict_t p_PyModule_GetDict = nullptr;

    PyRun_String_t p_PyRun_String = nullptr;

    PyCFunction_NewEx_t p_PyCFunction_NewEx = nullptr;

    PyObject* p_Py_None = nullptr;
    PyObject** p_PyExc_RuntimeError = nullptr;
}

namespace {

    bool g_loaded = false;

    // Python's shared library name embeds its version (python312.dll,
    // libpython3.12.so.1.0, ...), unlike R.dll's fixed name -- so unlike
    // r_dynlib.cpp, this has to discover which version is actually present
    // under pythonHome rather than open a hardcoded filename. Picks the
    // highest version found (a directory could in principle carry more than
    // one, e.g. leftover files from a prior install).
    struct Candidate
    {
        std::string fileName;
        int major;
        int minor;
    };

    bool parseVersionSuffix(const std::string& digits, int& major, int& minor)
    {
        // digits looks like "312" or "314" (Windows: no separator) or
        // "3.12"/"3.14" (POSIX: dot-separated) by the time callers here
        // strip it out -- both are normalized to "major, then the rest as
        // minor" before this runs.
        if (digits.size() < 2) return false;
        if (!std::isdigit(static_cast<unsigned char>(digits[0]))) return false;
        major = digits[0] - '0';
        std::string rest = digits.substr(1);
        for (char c : rest) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        if (rest.empty()) return false;
        minor = std::stoi(rest);
        return true;
    }

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

    // Finds the newest "python3NN.dll" directly under pythonHome (e.g.
    // C:/Python314/python314.dll) -- deliberately excludes the bare
    // "python3.dll" stable-ABI forwarder, since that DLL only *forwards* to
    // whichever versioned python3NN.dll is already loaded in the process;
    // it has nothing to forward to on its own.
    bool findNewestCandidate(const std::string& pythonHome, Candidate& out)
    {
        std::vector<Candidate> found;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(pythonHome, ec)) {
            if (ec || !entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            const std::string prefix = "python3";
            const std::string suffix = ".dll";
            if (lower.size() <= prefix.size() + suffix.size()) continue;
            if (lower.compare(0, prefix.size(), prefix) != 0) continue;
            if (lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
            std::string digits = "3" + lower.substr(prefix.size(), lower.size() - prefix.size() - suffix.size());
            int major = 0, minor = 0;
            if (!parseVersionSuffix(digits, major, minor)) continue;
            found.push_back({ name, major, minor });
        }
        if (found.empty()) return false;
        out = *std::max_element(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
            return a.minor < b.minor;
        });
        return true;
    }

    // Full path, NOT the bare filename r_dynlib.cpp's "R.dll" uses --
    // confirmed via a real failure this fixes: unlike R (whose bin directory
    // is genuinely prepended onto PATH before elara.exe is spawned, by
    // SessionRegistry's ensureRBinOnPath(), themisto/session_registry.cpp),
    // nothing adds python_home to carpo's own PATH, so a bare "python312.dll"
    // only ever resolved by accident, when some unrelated PATH entry (e.g. a
    // Python installer's own "add to PATH" step) happened to already cover
    // it -- a real per-user Python install under
    // %LOCALAPPDATA%\Python\pythoncore-3.12-64 (not on PATH by default) hit
    // exactly this and failed with "The specified module could not be
    // found." LoadLibraryA with a full path sidesteps needing PATH at all,
    // and (a second benefit, not just a workaround) Windows' safe DLL search
    // order also adds *that* directory when resolving the DLL's own
    // transitive dependencies -- the same thing PATH would have been for.
    LibHandle openPyLibrary(const std::string& pythonHome, std::string& outPath) {
        Candidate c;
        if (!findNewestCandidate(pythonHome, c)) {
            outPath.clear();
            return nullptr;
        }
        outPath = pythonHome + "\\" + c.fileName;
        return ::LoadLibraryA(outPath.c_str());
    }

    std::string describeOpenFailure(const std::string& pythonHome, const std::string& path) {
        if (path.empty()) {
            return "No python3NN.dll was found directly under python_home ('" + pythonHome +
                   "'). Is Python installed there? Configure python_home to the directory "
                   "containing pythonXY.dll (e.g. the root of a CPython install), or install "
                   "Python from https://www.python.org.";
        }
        DWORD err = ::GetLastError();
        return "Could not load " + path + " (" + formatLastError(err) + ") from python_home='" +
               pythonHome + "'.";
    }

    void* getSym(LibHandle handle, const char* name) {
        return reinterpret_cast<void*>(::GetProcAddress(handle, name));
    }
#else
    using LibHandle = void*;

    // POSIX Python installs put their shared library under lib/ (e.g.
    // /usr/lib/libpython3.12.so.1.0, matching `python3-config --ldflags`'s
    // own -L$prefix/lib -lpython3.12 report) -- unlike Windows, where it
    // sits directly under the install root. dlopen() doesn't consult $PATH
    // either way, so the full path is built explicitly, same reasoning as
    // r_dynlib.cpp's openRLibrary().
    bool findNewestCandidate(const std::string& libDir, Candidate& out)
    {
        std::vector<Candidate> found;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(libDir, ec)) {
            if (ec || !entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            const std::string prefix = "libpython3.";
            if (name.compare(0, prefix.size(), prefix) != 0) continue;
            std::string rest = name.substr(prefix.size());
            // rest looks like "12.so.1.0", "14.so", or "12.dylib" -- the
            // minor version is the leading digit run.
            std::size_t digitEnd = 0;
            while (digitEnd < rest.size() && std::isdigit(static_cast<unsigned char>(rest[digitEnd]))) {
                ++digitEnd;
            }
            if (digitEnd == 0) continue;
            std::string afterDigits = rest.substr(digitEnd);
            bool isSharedObject = afterDigits.rfind(".so", 0) == 0 || afterDigits == ".dylib" ||
                (afterDigits.rfind(".dylib", 0) == 0);
            if (!isSharedObject) continue;
            int minor = std::stoi(rest.substr(0, digitEnd));
            found.push_back({ name, 3, minor });
        }
        if (found.empty()) return false;
        out = *std::max_element(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) {
            return a.minor < b.minor;
        });
        return true;
    }

    LibHandle openPyLibrary(const std::string& pythonHome, std::string& outPath) {
        if (pythonHome.empty()) {
            outPath.clear();
            return nullptr;
        }
        // lib/ first (python.org / Homebrew / pyenv layouts), then the places
        // distribution packages use instead: lib64/ (Fedora/RHEL) and the
        // Debian/Ubuntu multiarch directory lib/<arch>-linux-gnu/ -- where
        // `apt install python3` puts libpython3.N.so.
        std::vector<std::string> libDirs = { pythonHome + "/lib", pythonHome + "/lib64" };
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(pythonHome + "/lib", ec)) {
            if (ec || !entry.is_directory()) continue;
            const std::string name = entry.path().filename().string();
            const std::string suffix = "-linux-gnu";
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                libDirs.push_back(entry.path().string());
            }
        }

        std::string libDir;
        Candidate c;
        bool found = false;
        for (const auto& dir : libDirs) {
            if (findNewestCandidate(dir, c)) {
                libDir = dir;
                found = true;
                break;
            }
        }
        if (!found) {
            outPath.clear();
            return nullptr;
        }
        outPath = libDir + "/" + c.fileName;
        // RTLD_GLOBAL, matching r_dynlib.cpp's libR.so rationale: Python C
        // extension modules imported later (numpy, etc.) are themselves
        // shared objects that reference libpython's symbols without linking
        // against it directly -- they depend on those symbols already being
        // visible process-wide from when this dlopen() first loaded it.
        return ::dlopen(outPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    }

    std::string describeOpenFailure(const std::string& pythonHome, const std::string& path) {
        const char* dlerror_msg = ::dlerror();
        std::string reason = dlerror_msg ? std::string(dlerror_msg) : "unknown error";
        if (path.empty()) {
            return "No libpython3.*.so*/.dylib was found under '" + pythonHome +
                   "/lib' (or lib64/, lib/<arch>-linux-gnu/). Is Python installed at '" + pythonHome + "'? Configure python_home to "
                   "a Python installation prefix (the directory containing lib/libpython3.*), or "
                   "install Python from https://www.python.org.";
        }
        return "Could not load " + path + " (" + reason + ").";
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
                "' -- this usually means an incompatible or corrupted Python installation. "
                "Try reinstalling Python from https://www.python.org.");
        }
        out = reinterpret_cast<FnPtr>(sym);
    }

} // namespace

bool isPyApiLoaded() { return g_loaded; }

void loadPyApi(const std::string& pythonHome) {
    if (g_loaded) return;

    std::string libPath;
    LibHandle handle = openPyLibrary(pythonHome, libPath);
    if (!handle) {
        throw std::runtime_error(describeOpenFailure(pythonHome, libPath));
    }

    using namespace api;
    resolve(handle, "Py_Initialize", p_Py_Initialize, libPath);
    resolve(handle, "Py_IsInitialized", p_Py_IsInitialized, libPath);
    resolve(handle, "Py_FinalizeEx", p_Py_FinalizeEx, libPath);

    resolve(handle, "PyErr_Occurred", p_PyErr_Occurred, libPath);
    resolve(handle, "PyErr_Fetch", p_PyErr_Fetch, libPath);
    resolve(handle, "PyErr_NormalizeException", p_PyErr_NormalizeException, libPath);
    resolve(handle, "PyErr_Clear", p_PyErr_Clear, libPath);
    resolve(handle, "PyErr_SetString", p_PyErr_SetString, libPath);

    resolve(handle, "PyObject_Str", p_PyObject_Str, libPath);
    resolve(handle, "PyObject_GetAttrString", p_PyObject_GetAttrString, libPath);
    resolve(handle, "PyObject_CallObject", p_PyObject_CallObject, libPath);

    resolve(handle, "Py_IncRef", p_Py_IncRef, libPath);
    resolve(handle, "Py_DecRef", p_Py_DecRef, libPath);

    resolve(handle, "PyUnicode_FromString", p_PyUnicode_FromString, libPath);
    resolve(handle, "PyUnicode_AsUTF8String", p_PyUnicode_AsUTF8String, libPath);
    resolve(handle, "PyBytes_AsString", p_PyBytes_AsString, libPath);

    resolve(handle, "PyTuple_New", p_PyTuple_New, libPath);
    resolve(handle, "PyTuple_SetItem", p_PyTuple_SetItem, libPath);
    resolve(handle, "PyTuple_GetItem", p_PyTuple_GetItem, libPath);

    resolve(handle, "PyDict_New", p_PyDict_New, libPath);
    resolve(handle, "PyDict_GetItemString", p_PyDict_GetItemString, libPath);
    resolve(handle, "PyDict_SetItemString", p_PyDict_SetItemString, libPath);

    resolve(handle, "PyList_Size", p_PyList_Size, libPath);
    resolve(handle, "PyList_GetItem", p_PyList_GetItem, libPath);

    resolve(handle, "PyLong_AsLong", p_PyLong_AsLong, libPath);
    resolve(handle, "PyLong_FromLong", p_PyLong_FromLong, libPath);

    resolve(handle, "PyImport_AddModule", p_PyImport_AddModule, libPath);
    resolve(handle, "PyModule_GetDict", p_PyModule_GetDict, libPath);

    resolve(handle, "PyRun_String", p_PyRun_String, libPath);

    resolve(handle, "PyCFunction_NewEx", p_PyCFunction_NewEx, libPath);

    // A DATA symbol, not a function -- "_Py_NoneStruct" is the actual
    // exported singleton object; Python's own headers only ever expose it
    // as the macro "Py_None" (#define Py_None (&_Py_NoneStruct)), never
    // under that name in the library itself. resolve()'s cast is still
    // correct here: dlsym/GetProcAddress already gives us the struct's
    // address directly, which is exactly what "Py_None" (a PyObject*)
    // needs to hold -- unlike R_NilValue/R_GlobalEnv in r_dynlib.cpp,
    // which are pointer *variables* (SEXP R_NilValue;) needing an extra
    // dereference, _Py_NoneStruct is the object itself.
    resolve(handle, "_Py_NoneStruct", p_Py_None, libPath);

    // A genuine pointer variable (see p_PyExc_RuntimeError's declaration
    // comment), so this resolves to a PyObject** directly -- no extra
    // dereference needed here, only at each use site (the PyExc_RuntimeError
    // macro).
    resolve(handle, "PyExc_RuntimeError", p_PyExc_RuntimeError, libPath);

    g_loaded = true;
}

} }
