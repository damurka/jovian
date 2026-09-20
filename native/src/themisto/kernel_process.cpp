#include "kernel_process.hpp"

#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace themisto
{
    namespace
    {
        std::string quoteArg(const std::string& value)
        {
            // Minimal Windows-style quoting -- arguments here are always
            // filesystem paths or generated keys/ids, never user-controlled
            // shell metacharacters, so this doesn't need to be exhaustive.
            std::string quoted = "\"";
            for (char c : value)
            {
                if (c == '"')
                {
                    quoted += '\\';
                }
                quoted += c;
            }
            quoted += "\"";
            return quoted;
        }

        std::vector<std::pair<std::string, std::string>> toArgPairs(const KernelProcessOptions& options)
        {
            return {
                { "--r-home", options.rHome },
                { "--r-path", options.rPath },
                { "--r-libs", options.rLibs },
                { "--pandoc-path", options.pandocPath },
                { "--hera-src-path", options.heraSrcPath },
                { "--python-home", options.pythonHome },
                { "--python-path", options.pythonPath },
                { "--venv-path", options.venvPath },
                { "--registration-ip", options.registrationIp },
                { "--registration-port", options.registrationPort },
                { "--key", options.key },
            };
        }

        // One job object shared by every kernel this supervisor process
        // ever spawns, created lazily on first use. JOB_OBJECT_LIMIT_KILL_
        // ON_JOB_CLOSE means the OS itself force-kills every process still
        // assigned to this job the moment the job's last handle closes --
        // which happens automatically when *this* process (the only thing
        // holding that handle) exits, by any means: a clean stopAll(),
        // the process.once('exit') fallback in session-manager.ts, a crash,
        // or Task Manager "End Task". That's the actual fix for a real,
        // reported bug: orphaned elara.exe processes surviving even
        // a full VS Code close. Everything upstream of this (Session.kill()
        // only closing a local WebSocket, the exit handler only killing
        // *this* process) was cooperative cleanup that depended on code
        // actually running before exit -- fragile by construction, since
        // Windows does not kill child processes when their parent dies
        // unless something explicitly arranges it. A job object is that
        // arrangement, enforced by the OS, not by any cleanup code path
        // actually executing.
        //
        // Windows-only (Job Objects): its one call site, in start()'s
        // CreateProcess branch below, is already inside an #ifdef _WIN32
        // block, but this definition wasn't -- HANDLE and friends don't
        // exist on POSIX, so the whole file failed to even compile there.
#ifdef _WIN32
        HANDLE getKernelJobObject()
        {
            static HANDLE job = []() -> HANDLE {
                HANDLE h = CreateJobObjectA(nullptr, nullptr);
                if (!h)
                {
                    return nullptr;
                }
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info)))
                {
                    CloseHandle(h);
                    return nullptr;
                }
                return h;
            }();
            return job;
        }
#endif
    }

    namespace
    {
        // Was hardcoded to "elara" until Carpo became a second real,
        // spawnable kernel type -- a Python session's own output was then
        // confusingly relayed under an "[elara]" prefix (confirmed directly
        // via a real end-to-end supervisor run). Derived from the actual
        // spawned executable's filename instead, stripping a Windows
        // ".exe" suffix so both platforms print the same bare name.
        std::string outputPumpLabel(const std::string& kernelExePath)
        {
            std::size_t slash = kernelExePath.find_last_of("/\\");
            std::string name = slash == std::string::npos ? kernelExePath : kernelExePath.substr(slash + 1);
            const std::string exeSuffix = ".exe";
            if (name.size() > exeSuffix.size() &&
                name.compare(name.size() - exeSuffix.size(), exeSuffix.size(), exeSuffix) == 0)
            {
                name.resize(name.size() - exeSuffix.size());
            }
            return name.empty() ? "kernel" : name;
        }
    }

    KernelProcess::KernelProcess(const KernelProcessOptions& options) : m_options(options) {}

    KernelProcess::~KernelProcess()
    {
        kill();
    }

    // Reads lines from the kernel's stdout/stderr (redirected into one pipe
    // by start()) and relays them to the supervisor's own stderr, prefixed
    // for attribution -- without this there is no visibility at all into
    // why a spawned kernel failed to reach registration (R startup errors,
    // package load failures, etc. would otherwise vanish into a pipe no one
    // reads).
    void KernelProcess::startOutputPump(void* readHandle)
    {
        m_running = true;
        std::string label = outputPumpLabel(m_options.kernelExePath);
#ifdef _WIN32
        HANDLE handle = static_cast<HANDLE>(readHandle);
        m_outputThread = std::thread([this, handle, label]() {
            char buffer[4096];
            std::string carry;
            DWORD bytesRead = 0;
            while (m_running && ReadFile(handle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                carry.append(buffer, bytesRead);
                std::size_t pos;
                while ((pos = carry.find('\n')) != std::string::npos)
                {
                    std::cerr << "[" << label << "] " << carry.substr(0, pos) << std::endl;
                    carry.erase(0, pos + 1);
                }
            }
            if (!carry.empty())
            {
                std::cerr << "[" << label << "] " << carry << std::endl;
            }
            CloseHandle(handle);
        });
#else
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(readHandle));
        m_outputThread = std::thread([this, fd, label]() {
            char buffer[4096];
            std::string carry;
            ssize_t bytesRead;
            while (m_running && (bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
            {
                carry.append(buffer, static_cast<std::size_t>(bytesRead));
                std::size_t pos;
                while ((pos = carry.find('\n')) != std::string::npos)
                {
                    std::cerr << "[" << label << "] " << carry.substr(0, pos) << std::endl;
                    carry.erase(0, pos + 1);
                }
            }
            if (!carry.empty())
            {
                std::cerr << "[" << label << "] " << carry << std::endl;
            }
            close(fd);
        });
#endif
    }

#ifdef _WIN32
    void KernelProcess::start()
    {
        std::ostringstream cmd;
        cmd << quoteArg(m_options.kernelExePath);
        for (const auto& [flag, value] : toArgPairs(m_options))
        {
            if (value.empty())
            {
                continue;
            }
            cmd << " " << flag << " " << quoteArg(value);
        }
        std::string commandLine = cmd.str();

        SECURITY_ATTRIBUTES pipeAttrs{};
        pipeAttrs.nLength = sizeof(pipeAttrs);
        pipeAttrs.bInheritHandle = TRUE;

        HANDLE readHandle = nullptr;
        HANDLE writeHandle = nullptr;
        if (!CreatePipe(&readHandle, &writeHandle, &pipeAttrs, 0))
        {
            throw std::runtime_error("Failed to create output pipe for elara process");
        }
        // The write end must not be inherited by the *supervisor* itself
        // (only by the child, via STARTUPINFOA below) -- otherwise the pipe
        // never sees EOF after the child exits, since the supervisor would
        // still be holding its own copy of the write handle open.
        SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);

        // Plain bInheritHandles=TRUE doesn't just hand the child hStdOutput/
        // hStdError -- it inherits *every* currently-inheritable handle in
        // this process (any other open pipe end, ZMQ-internal handles,
        // etc). If the child ends up with an extra copy of writeHandle (or
        // anything else backing this pipe) through that side door, this
        // process's ReadFile() in startOutputPump() never sees EOF even
        // after the child exits, since some handle to the write end is
        // still open somewhere -- found via test/session_registry_test.cpp
        // as an intermittent post-test hang with the kernel process already
        // gone. PROC_THREAD_ATTRIBUTE_HANDLE_LIST restricts inheritance to
        // exactly the one handle the child actually needs.
        SIZE_T attrListSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
        std::vector<char> attrListBuffer(attrListSize);
        auto* attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrListBuffer.data());
        if (!InitializeProcThreadAttributeList(attrList, 1, 0, &attrListSize))
        {
            CloseHandle(writeHandle);
            CloseHandle(readHandle);
            throw std::runtime_error("Failed to initialize process attribute list for elara process");
        }
        HANDLE inheritList[] = { writeHandle };
        if (!UpdateProcThreadAttribute(
                attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inheritList, sizeof(inheritList), nullptr, nullptr))
        {
            DeleteProcThreadAttributeList(attrList);
            CloseHandle(writeHandle);
            CloseHandle(readHandle);
            throw std::runtime_error("Failed to set inherited handle list for elara process");
        }

        STARTUPINFOEXA startupInfo{};
        startupInfo.StartupInfo.cb = sizeof(startupInfo);
        startupInfo.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.StartupInfo.hStdOutput = writeHandle;
        startupInfo.StartupInfo.hStdError = writeHandle;
        startupInfo.StartupInfo.hStdInput = nullptr;
        startupInfo.lpAttributeList = attrList;
        PROCESS_INFORMATION processInfo{};

        // CREATE_NO_WINDOW: the supervisor itself may be spawned headlessly
        // (e.g. from VS Code's Shared Process, same reasoning that led
        // session-manager.ts to avoid inheriting stdio for the addon-based
        // child) -- kernel processes should never pop a console window.
        // bInheritHandles=TRUE is still required for the handle list above
        // to take effect; EXTENDED_STARTUPINFO_PRESENT is what makes
        // CreateProcess honor lpAttributeList at all.
        BOOL ok = CreateProcessA(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr,
            nullptr,
            &startupInfo.StartupInfo,
            &processInfo);

        DeleteProcThreadAttributeList(attrList);

        // This process's copy of the write end must be closed regardless of
        // outcome -- on success the child owns the handle it inherited; on
        // failure there's nothing to write to it for.
        CloseHandle(writeHandle);

        if (!ok)
        {
            CloseHandle(readHandle);
            throw std::runtime_error("Failed to spawn elara process (CreateProcess failed)");
        }

        m_processHandle = processInfo.hProcess;
        m_processId = processInfo.dwProcessId;
        CloseHandle(processInfo.hThread);

        // Best-effort: if this fails (e.g. a pre-Windows-8 host with no
        // nested-job support, vanishingly unlikely on any real target here),
        // the kernel still runs fine standalone -- it just loses the
        // guaranteed-cleanup-on-supervisor-death property, no worse than
        // before this existed.
        if (HANDLE job = getKernelJobObject())
        {
            AssignProcessToJobObject(job, static_cast<HANDLE>(m_processHandle));
        }

        startOutputPump(readHandle);
    }

    bool KernelProcess::isAlive() const
    {
        if (!m_processHandle)
        {
            return false;
        }
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(static_cast<HANDLE>(m_processHandle), &exitCode))
        {
            return false;
        }
        return exitCode == STILL_ACTIVE;
    }

    void KernelProcess::kill()
    {
        m_running = false;
        if (m_processHandle)
        {
            TerminateProcess(static_cast<HANDLE>(m_processHandle), 1);
            CloseHandle(static_cast<HANDLE>(m_processHandle));
            m_processHandle = nullptr;
        }
        if (m_outputThread.joinable())
        {
            m_outputThread.join();
        }
    }

    std::string KernelProcess::describeStatus() const
    {
        if (!m_processHandle)
        {
            return "process was never started or has already been cleaned up";
        }
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(static_cast<HANDLE>(m_processHandle), &exitCode))
        {
            return "unable to query process exit code (GetExitCodeProcess failed)";
        }
        if (exitCode == STILL_ACTIVE)
        {
            return "process is still running -- likely hung or blocked rather than crashed";
        }

        std::ostringstream oss;
        oss << "process exited with code 0x" << std::hex << exitCode;
        switch (exitCode)
        {
        case 0xC0000005:
            oss << " (STATUS_ACCESS_VIOLATION -- a native crash, e.g. in a compiled R package)";
            break;
        case 0xC00000FD:
            oss << " (STATUS_STACK_OVERFLOW)";
            break;
        case 0xC0000409:
            oss << " (STATUS_STACK_BUFFER_OVERRUN)";
            break;
        case 0xC0000135:
            oss << " (STATUS_DLL_NOT_FOUND -- a required DLL is missing)";
            break;
        case 0xC000007B:
            oss << " (STATUS_INVALID_IMAGE_FORMAT -- likely a 32/64-bit or ABI mismatch in a loaded DLL)";
            break;
        case 0xC0000142:
            oss << " (STATUS_DLL_INIT_FAILED)";
            break;
        default:
            break;
        }
        return oss.str();
    }
#else
    void KernelProcess::start()
    {
        std::vector<std::string> argStorage = { m_options.kernelExePath };
        for (const auto& [flag, value] : toArgPairs(m_options))
        {
            if (value.empty())
            {
                continue;
            }
            argStorage.push_back(flag);
            argStorage.push_back(value);
        }

        std::vector<char*> argv;
        argv.reserve(argStorage.size() + 1);
        for (auto& arg : argStorage)
        {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        int pipeFds[2];
        if (pipe(pipeFds) != 0)
        {
            throw std::runtime_error("Failed to create output pipe for elara process");
        }

        // The "self-pipe trick": fork() succeeding only means the OS made a
        // new process, not that it's actually running this executable --
        // execv() itself runs in the child, and if it fails there (e.g. a
        // nonexistent path), that failure previously vanished into a
        // process that just exits with code 127 moments later, with no way
        // for the parent to distinguish that synchronously from "started
        // fine and exited almost immediately for some other reason" (unlike
        // the Windows branch above, where CreateProcess's own return value
        // reports this directly). FD_CLOEXEC on the write end means a
        // successful execv() closes it automatically; the parent's
        // blocking read() then returns 0 (EOF, nothing was ever written).
        // If execv() fails instead, it returns, and the child writes errno
        // before exiting -- the parent's read() then returns that errno.
        int execStatusFds[2];
        if (pipe(execStatusFds) != 0)
        {
            close(pipeFds[0]);
            close(pipeFds[1]);
            throw std::runtime_error("Failed to create exec-status pipe for elara process");
        }
        fcntl(execStatusFds[1], F_SETFD, FD_CLOEXEC);

        pid_t pid = fork();
        if (pid < 0)
        {
            close(pipeFds[0]);
            close(pipeFds[1]);
            close(execStatusFds[0]);
            close(execStatusFds[1]);
            throw std::runtime_error("Failed to fork elara process");
        }
        if (pid == 0)
        {
            close(pipeFds[0]);
            close(execStatusFds[0]);
            dup2(pipeFds[1], STDOUT_FILENO);
            dup2(pipeFds[1], STDERR_FILENO);
            close(pipeFds[1]);

            // Real, reproduced CI failure this fixes: elara dlopen()s
            // libR.so by its full path directly (r_dynlib.cpp), which needs
            // no LD_LIBRARY_PATH entry for that one call -- but R's own base
            // packages (utils.so, methods.so, ...) are themselves shared
            // objects with libR.so as a plain, unqualified NEEDED entry,
            // loaded later via R's own dyn.load(). Resolving that bare name
            // needs LD_LIBRARY_PATH/DYLD_LIBRARY_PATH to find it, and that
            // has to be set here, in the child, before exec() -- NOT inside
            // elara's own main() (a first attempt at that lived in
            // elara::Server::setupEnvironment(), engine.cpp, and didn't
            // work): the dynamic linker builds its search path once, at
            // process startup, before main() runs, so a setenv() from
            // within an already-running process has no effect on it.
            // Setting it here, between fork() and exec(), means the *new*
            // process image's own linker startup sees it from the start.
            // Without this, every one of R's own base packages fails to
            // load, R limps on with none of its default packages (utils/
            // methods/stats/...), and hera's own .Call()s into those
            // missing packages then fail or (confirmed: a real segfault on
            // macOS, from an unrelated bug this also happened to mask)
            // crash outright instead of failing cleanly.
            // Python (carpo) sessions are exposed to the exact same class of
            // bug, pre-emptively: py_dynlib.cpp dlopen()s libpythonX.Y.so by
            // its full path directly too, but Python's own C-extension
            // modules (including several of the standard library's own --
            // _socket, _ssl, _json, ... -- not just third-party ones) are
            // themselves shared objects with libpythonX.Y.so as a plain,
            // unqualified NEEDED entry, loaded later via Python's own import
            // machinery. Not yet confirmed to fail the same way R's base
            // packages did (this hasn't been exercised on Linux/macOS CI as
            // of this fix), but the mechanism is identical enough, and the
            // cost of being wrong (found the hard way, again, on some future
            // CI run or user's machine) high enough, to apply the same fix
            // proactively rather than wait for a second reproduction.
            std::vector<std::string> libDirs;
            if (!m_options.rHome.empty())
            {
                libDirs.push_back(m_options.rHome + "/lib");
            }
            if (!m_options.pythonHome.empty())
            {
                libDirs.push_back(m_options.pythonHome + "/lib");
            }

            if (!libDirs.empty())
            {
                std::string combined;
                for (const auto& dir : libDirs)
                {
                    if (!combined.empty()) combined += ":";
                    combined += dir;
                }

#ifdef __APPLE__
                const char* ldPathVar = "DYLD_LIBRARY_PATH";
#else
                const char* ldPathVar = "LD_LIBRARY_PATH";
#endif
                const char* existingLdPath = getenv(ldPathVar);
                std::string newLdPath = existingLdPath && *existingLdPath
                    ? combined + ":" + existingLdPath
                    : combined;
                setenv(ldPathVar, newLdPath.c_str(), 1);
            }

            execv(m_options.kernelExePath.c_str(), argv.data());
            int execErrno = errno;
            // Best-effort: if this write is ever short/interrupted, the
            // parent's read() below still detects *some* failure (it gets
            // 0 < n < sizeof(int) bytes, still != 0), just possibly without
            // a decodable errno -- still strictly better than reporting
            // success.
            (void)write(execStatusFds[1], &execErrno, sizeof(execErrno));
            _exit(127);
        }

        close(pipeFds[1]);
        close(execStatusFds[1]);

        int execErrno = 0;
        ssize_t bytesRead = read(execStatusFds[0], &execErrno, sizeof(execErrno));
        close(execStatusFds[0]);
        if (bytesRead > 0)
        {
            close(pipeFds[0]);
            waitpid(pid, nullptr, 0); // reap the child so it doesn't linger as a zombie
            throw std::runtime_error("Failed to spawn elara process (execv failed: " + std::string(std::strerror(execErrno)) + ")");
        }

        m_processId = pid;
        m_stdoutFd = pipeFds[0];
        startOutputPump(reinterpret_cast<void*>(static_cast<intptr_t>(m_stdoutFd)));
    }

    pid_t KernelProcess::waitpidCached(int options) const
    {
        if (m_reaped)
        {
            return m_processId;
        }
        int status = 0;
        pid_t result = waitpid(m_processId, &status, options);
        if (result == m_processId)
        {
            m_reaped = true;
            m_exitStatus = status;
        }
        return result;
    }

    bool KernelProcess::isAlive() const
    {
        if (m_processId <= 0)
        {
            return false;
        }
        return waitpidCached(WNOHANG) == 0;
    }

    void KernelProcess::kill()
    {
        m_running = false;
        if (m_processId > 0)
        {
            ::kill(m_processId, SIGKILL);
            waitpidCached(0); // blocking; a no-op if already reaped (e.g. isAlive() got there first)
            m_processId = -1;
        }
        if (m_outputThread.joinable())
        {
            m_outputThread.join();
        }
    }

    std::string KernelProcess::describeStatus() const
    {
        if (m_processId <= 0)
        {
            return "process was never started or has already been cleaned up";
        }
        pid_t result = waitpidCached(WNOHANG | WUNTRACED);
        if (result == 0)
        {
            return "process is still running -- likely hung or blocked rather than crashed";
        }
        if (result < 0)
        {
            return "unable to query process status (waitpid failed)";
        }
        int status = m_exitStatus;
        std::ostringstream oss;
        if (WIFEXITED(status))
        {
            oss << "process exited with code " << WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            oss << "process terminated by signal " << WTERMSIG(status);
            if (WTERMSIG(status) == SIGSEGV)
            {
                oss << " (SIGSEGV -- a native crash, e.g. in a compiled R package)";
            }
            else if (WTERMSIG(status) == SIGABRT)
            {
                oss << " (SIGABRT)";
            }
        }
        else
        {
            oss << "process status unknown (raw status " << status << ")";
        }
        return oss.str();
    }
#endif
}
