#include "callisto/stata/stata_dynlib.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace callisto { namespace stata {

namespace api {
    StataSO_Main_t p_StataSO_Main = nullptr;
    StataSO_Execute_t p_StataSO_Execute = nullptr;
    StataSO_GetOutputBuffer_t p_StataSO_GetOutputBuffer = nullptr;
    StataSO_ClearOutputBuffer_t p_StataSO_ClearOutputBuffer = nullptr;
    StataSO_SetBreak_t p_StataSO_SetBreak = nullptr;
    StataSO_Shutdown_t p_StataSO_Shutdown = nullptr;
}

namespace {

    bool g_loaded = false;

    // Where each edition's library sits under the Stata directory, in the
    // order pystata's config._find_lib() looks (Linux also has the older
    // distn/ layouts).
    std::vector<std::filesystem::path> candidatePaths(const std::filesystem::path& home, const std::string& edition)
    {
#if defined(_WIN32)
        return { home / (edition + "-64.dll") };
#elif defined(__APPLE__)
        std::string app = edition == "be" ? "StataBE.app" : edition == "se" ? "StataSE.app" : "StataMP.app";
        return { home / app / "Contents" / "MacOS" / ("libstata-" + edition + ".dylib") };
#else
        std::string name = edition == "be" ? "libstata.so" : "libstata-" + edition + ".so";
        return {
            home / name,
            home / ".." / "distn" / "linux64" / name,
            home / ".." / "distn" / "linux.64p" / name,
            home / ".." / "distn" / "linux.64" / name,
        };
#endif
    }

#ifdef _WIN32
    using LibHandle = HMODULE;

    std::string formatLastError(DWORD code)
    {
        char* buf = nullptr;
        DWORD len = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<char*>(&buf), 0, nullptr);
        std::string message = (len > 0 && buf) ? std::string(buf, len) : "unknown error";
        if (buf) LocalFree(buf);
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        {
            message.pop_back();
        }
        return message;
    }

    // LOAD_WITH_ALTERED_SEARCH_PATH: the DLLs mp-64.dll itself depends on
    // are looked for in its own directory first, not only on PATH.
    LibHandle openLibrary(const std::filesystem::path& path, std::string& error)
    {
        HMODULE handle = ::LoadLibraryExW(path.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!handle)
        {
            error = formatLastError(::GetLastError());
        }
        return handle;
    }

    void* getSym(LibHandle handle, const char* name)
    {
        return reinterpret_cast<void*>(::GetProcAddress(handle, name));
    }
#else
    using LibHandle = void*;

    LibHandle openLibrary(const std::filesystem::path& path, std::string& error)
    {
        void* handle = ::dlopen(path.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handle)
        {
            const char* message = ::dlerror();
            error = message ? message : "unknown error";
        }
        return handle;
    }

    void* getSym(LibHandle handle, const char* name)
    {
        return ::dlsym(handle, name);
    }
#endif

    template <typename FnPtr>
    void resolve(LibHandle handle, const char* name, FnPtr& out, const std::string& libPath)
    {
        void* sym = getSym(handle, name);
        if (!sym)
        {
            throw std::runtime_error(
                std::string("Stata's library ") + libPath + " does not export " + name +
                " -- it needs Stata 17 or newer.");
        }
        out = reinterpret_cast<FnPtr>(sym);
    }

} // namespace

Library loadStataApi(const std::string& stataHome, const std::string& edition)
{
    static Library loaded;
    if (g_loaded) return loaded;

    if (stataHome.empty())
    {
        throw std::runtime_error(
            "No Stata installation was given -- pass stataHome (the directory holding Stata's "
            "executable) or set STATA_HOME.");
    }
    std::error_code ec;
    // Absolute: LoadLibraryEx's LOAD_WITH_ALTERED_SEARCH_PATH needs a full
    // path, and error messages should say where exactly it looked.
    std::filesystem::path home = std::filesystem::absolute(stataHome, ec);
    if (ec || !std::filesystem::is_directory(home, ec))
    {
        throw std::runtime_error("Stata directory not found: " + stataHome);
    }

    auto firstPresent = [&](const std::string& ed) -> std::filesystem::path {
        for (const auto& candidate : candidatePaths(home, ed))
        {
            if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
        }
        return {};
    };
    std::vector<std::string> installed;
    for (const char* ed : { "mp", "se", "be" })
    {
        if (!firstPresent(ed).empty()) installed.push_back(ed);
    }

    std::vector<std::string> editions = edition.empty() ? std::vector<std::string>{ "mp", "se", "be" }
                                                        : std::vector<std::string>{ edition };
    std::vector<std::string> tried;
    for (const auto& ed : editions)
    {
        for (const auto& candidate : candidatePaths(home, ed))
        {
            if (!std::filesystem::is_regular_file(candidate, ec))
            {
                tried.push_back(candidate.lexically_normal().string());
                continue;
            }

            std::string libPath = candidate.lexically_normal().string();
            std::string error;
            LibHandle handle = openLibrary(candidate, error);
            if (!handle)
            {
                throw std::runtime_error("Could not load Stata's library " + libPath + ": " + error);
            }

            using namespace api;
            resolve(handle, "StataSO_Main", p_StataSO_Main, libPath);
            resolve(handle, "StataSO_Execute", p_StataSO_Execute, libPath);
            resolve(handle, "StataSO_GetOutputBuffer", p_StataSO_GetOutputBuffer, libPath);
            resolve(handle, "StataSO_ClearOutputBuffer", p_StataSO_ClearOutputBuffer, libPath);
            resolve(handle, "StataSO_SetBreak", p_StataSO_SetBreak, libPath);
            resolve(handle, "StataSO_Shutdown", p_StataSO_Shutdown, libPath);

            loaded = Library{ libPath, ed, installed };
            g_loaded = true;
            return loaded;
        }
    }

    std::string message = "No Stata shared library found in " + stataHome +
                          " (Stata 17 or newer is needed). Looked for:";
    for (const auto& path : tried)
    {
        message += "\n  " + path;
    }
    throw std::runtime_error(message);
}

bool isStataApiLoaded()
{
    return g_loaded;
}

} }
