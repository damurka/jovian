#ifndef THEMISTO_KERNEL_PROCESS_HPP
#define THEMISTO_KERNEL_PROCESS_HPP

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace themisto
{
    struct KernelProcessOptions
    {
        std::string kernelExePath;
        std::string rHome;
        std::string rPath;
        std::string rLibs;
        std::string pandocPath;
        std::string heraSrcPath;
        // Carpo (Python) equivalents of the R fields above -- only one set
        // is ever non-empty for a given process (SessionRegistry only fills
        // in the fields matching SessionOptions::kernelType), but both live
        // on this one struct rather than a variant: toArgPairs()
        // (kernel_process.cpp) already skips empty values when building
        // argv, so passing both unconditionally costs nothing and avoids
        // needing a kernel-type switch here too.
        std::string pythonHome;
        std::string pythonPath;
        std::string venvPath;
        std::string registrationIp;
        std::string registrationPort;
        std::string key;
    };

    // Spawns/owns one `elara` child process. Deliberately minimal --
    // the supervisor never talks to the kernel through this class after
    // spawning it; all runtime traffic (execute, interrupt, shutdown) goes
    // over the ZMQ ClientZmq connection established once the kernel
    // registers (see session_registry.cpp). This class only covers what
    // process supervision needs: start, liveness, and a last-resort kill.
    class KernelProcess
    {
    public:
        explicit KernelProcess(const KernelProcessOptions& options);
        ~KernelProcess();

        KernelProcess(const KernelProcess&) = delete;
        KernelProcess& operator=(const KernelProcess&) = delete;

        void start();
        bool isAlive() const;
        void kill();

        // Diagnostic description for when a kernel is declared dead via
        // heartbeat timeout (see SessionRegistry's kernel-status listener):
        // distinguishes a genuine process exit (with its exit code, decoded
        // for common native-crash codes where recognized) from a kernel
        // that's still running but simply didn't answer heartbeat pings in
        // time -- these look identical from the "no more pongs" signal
        // alone, but call for very different debugging (a real crash vs. a
        // hang/deadlock/long blocking call).
        std::string describeStatus() const;

    private:
        void startOutputPump(void* readHandle);

#ifndef _WIN32
        // Wraps waitpid(m_processId, ...), caching the result the first
        // time it successfully reaps the child. POSIX only allows a
        // zombie's exit status to be retrieved once -- isAlive(),
        // describeStatus() and kill() can each be called, in any order,
        // including after one another, and all need that same answer.
        // Same tri-state contract as waitpid() itself: m_processId (a
        // positive reap, whether just now or previously cached), 0 (still
        // running -- only possible with WNOHANG and no cached reap yet),
        // or -1 (error).
        pid_t waitpidCached(int options) const;
#endif

        KernelProcessOptions m_options;
        std::thread m_outputThread;
        std::atomic<bool> m_running{ false };

#ifdef _WIN32
        void* m_processHandle = nullptr;
        unsigned long m_processId = 0;
#else
        int m_processId = -1;
        int m_stdoutFd = -1;
        mutable bool m_reaped = false;
        mutable int m_exitStatus = 0;
#endif
    };
}

#endif
