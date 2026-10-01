#ifndef ELARA_LOG_HPP
#define ELARA_LOG_HPP

// The R kernel's own log: one line per event on stderr, "<level>: <message>" (Themisto, which relays a kernel's
// output, puts "[elara]" before it). Jovian shows that output at its debug level (JOVIAN_LOG_LEVEL=debug, or
// JOVIAN_KERNEL_OUTPUT) and quotes the "error"/"warning" lines when a kernel exits unexpectedly
// (describeKernelExit() in supervisor-client.ts).
//
// ELARA_LOG_LEVEL -- debug, info (the default), warning or error -- sets which levels are written; Jovian sets it to
// debug when its own level is debug or trace. hera logs through it too (the elara_log routine). Every Jupyter
// message in and out is logged as well when ADRASTEA_LOG is set (Adrastea's message logger).

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <io.h>
#endif

namespace elara
{
    namespace log
    {
        enum class Level { debug = 0, info = 1, warning = 2, error = 3 };

        inline Level parseLevel(const std::string &text, Level fallback)
        {
            if (text == "debug" || text == "trace") return Level::debug;
            if (text == "info" || text == "notice") return Level::info;
            if (text == "warning" || text == "warn") return Level::warning;
            if (text == "error" || text == "fatal") return Level::error;
            return fallback;
        }

        inline Level threshold()
        {
            static const Level level = [] {
                const char *value = std::getenv("ELARA_LOG_LEVEL");
                return value ? parseLevel(value, Level::info) : Level::info;
            }();
            return level;
        }

        inline const char *name(Level level)
        {
            switch (level)
            {
            case Level::debug: return "debug";
            case Level::info: return "info";
            case Level::warning: return "warning";
            default: return "error";
            }
        }

        // Where log lines go: the kernel's stderr as it was started (the pipe Themisto reads).
        inline FILE *&sink()
        {
            static FILE *file = stderr;
            return file;
        }

        // Windows: once R starts, Elara points stdout and stderr at a hidden console (initEmbeddedRWindows(), so a
        // package's C printf() has somewhere to go) -- and log lines written there were lost. Called just before, this
        // keeps a copy of the stderr the kernel was started with for the log.
        inline void keepCurrentStderr()
        {
#ifdef _WIN32
            int fd = _dup(_fileno(stderr));
            if (fd >= 0)
            {
                if (FILE *file = _fdopen(fd, "w")) sink() = file;
            }
#endif
        }

        inline bool enabled(Level level)
        {
            return level >= threshold();
        }

        inline void write(Level level, const std::string &message)
        {
            if (!enabled(level)) return;
            // one line: R's error text ends with a newline of its own
            auto end = message.find_last_not_of(" \t\r\n");
            std::string line = end == std::string::npos ? std::string() : message.substr(0, end + 1);
            static std::mutex mutex;
            std::lock_guard<std::mutex> lock(mutex);
            std::fprintf(sink(), "%s: %s\n", name(level), line.c_str());
            std::fflush(sink());
        }

        inline void debug(const std::string &message) { write(Level::debug, message); }
        inline void info(const std::string &message) { write(Level::info, message); }
        inline void warning(const std::string &message) { write(Level::warning, message); }
        inline void error(const std::string &message) { write(Level::error, message); }
    }
}

#endif // ELARA_LOG_HPP
