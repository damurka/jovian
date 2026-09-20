#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include "Windows.h"
#include <algorithm>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#  include <cstdint>
#  include <mach-o/dyld.h>
#endif

#include "adrastea/system.hpp"

namespace adrastea 
{
    std::string removeEndingSeparator(const char* path)
    {
        std::size_t s = strlen(path);
        if (path[s - 1] == '/')
        {
            --s;
        }
        return std::string(path, s);
    }

    std::string getTempDirectoryPathImpl()
    {
#ifdef _WIN32
        std::string tmp_prefix;
        char char_path[MAX_PATH];
        if (auto s = GetTempPathA(MAX_PATH, char_path))
        {
            tmp_prefix = std::string(char_path, std::size_t(s - 1));
        }
        std::replace(tmp_prefix.begin(), tmp_prefix.end(), '\\', '/');
        return tmp_prefix;
#else
        const char* tmpdir = std::getenv("TMPDIR");
        const char* tmp = std::getenv("TMP");
        const char* tempdir = std::getenv("TEMPDIR");
        const char* temp = std::getenv("TEMP");
        if (tmpdir != nullptr) return removeEndingSeparator(tmpdir);
        else if (tmp != nullptr) return removeEndingSeparator(tmp);
        else if (tempdir != nullptr) return removeEndingSeparator(tempdir);
        else if (temp != nullptr) return removeEndingSeparator(temp);
        else return "/tmp";
#endif
    }

    std::string getTempDirectoryPath()
    {
        static const std::string path = getTempDirectoryPathImpl();
        return path;
    }

    bool createDirectory(const std::string& path)
    {
        std::size_t pos = path.rfind('/');
        if (pos != 0 && pos != std::string::npos)
        {
            createDirectory(path.substr(0, pos));
        }
#ifdef _WIN32
        return CreateDirectoryA(path.c_str(), NULL);
#else
        struct stat st;
        st.st_dev = 0;
        int ret = 0;
        if (stat(path.c_str(), &st) == -1)
        {
            ret = mkdir(path.c_str(), 0700);
        }
        return ret == 0;
#endif
    }

    int getCurrentPid()
    {
#ifdef _WIN32
        return GetCurrentProcessId();
#else
        return ::getpid();
#endif
    }

    std::string getCellTmpFile(const std::string& prefix,
        int execution_count,
        const std::string& extension)
    {
        return prefix + "/[" + std::to_string(execution_count) + "]" + extension;
    }

    std::size_t getTmpHashSeed()
    {
        std::size_t hash_seed(0xc70f6907UL);
        return hash_seed;
    }

    std::string getTmpPrefix(const std::string& process_name)
    {
        std::string tmp_prefix = adrastea::getTempDirectoryPath()
            + '/' + process_name + '_'
            + std::to_string(adrastea::getCurrentPid())
            + '/';
        return tmp_prefix;
    }

    std::string executablePath()
    {
        std::string path;
#if defined(UNICODE)
        wchar_t buffer[1024];
#else
        char buffer[1024];
#endif
        std::memset(buffer, '\0', sizeof(buffer));
#if defined(__linux__)
        if (readlink("/proc/self/exe", buffer, sizeof(buffer)) != -1)
        {
            path = buffer;
        }
        else
        {
            // failed to determine run path
        }
#elif defined (_WIN32)
#if defined(UNICODE)
        if (GetModuleFileNameW(nullptr, buffer, sizeof(buffer)) != 0)
        {
            // Convert wchar_t to std::string
            std::wstring wideString(buffer);
            std::string narrowString(wideString.begin(), wideString.end());
            path = narrowString;
        }
#else
        if (GetModuleFileNameA(nullptr, buffer, sizeof(buffer)) != 0)
        {
            path = buffer;
        }
#endif
        // failed to determine run path
#elif defined (__APPLE__)
        std::uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0)
        {
            path = buffer;
        }
        else
        {
            // failed to determine run path
        }
#elif defined (__FreeBSD__)
        int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
        size_t buffer_size = sizeof(buffer);
        if (sysctl(mib, 4, buffer, &buffer_size, NULL, 0) != -1)
        {
            path = buffer;
        }
        else
        {
            // failed to determine run path
        }
#endif
        return path;
    }

    std::string prefixPath()
    {
        std::string path = executablePath();
#if defined (_WIN32)
        char separator = '\\';
#else
        char separator = '/';
#endif
        std::string bin_folder = path.substr(0, path.find_last_of(separator));
        std::string prefix = bin_folder.substr(0, bin_folder.find_last_of(separator)) + separator;
        return prefix;
    }
}
