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
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace datasuite::supervisor
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
        // reported bug: orphaned datasuite-r.exe processes surviving even
        // a full VS Code close. Everything upstream of this (Session.kill()
        // only closing a local WebSocket, the exit handler only killing
        // *this* process) was cooperative cleanup that depended on code
        // actually running before exit -- fragile by construction, since
        // Windows does not kill child processes when their parent dies
        // unless something explicitly arranges it. A job object is that
        // arrangement, enforced by the OS, not by any cleanup code path
        // actually executing.
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
#ifdef _WIN32
        HANDLE handle = static_cast<HANDLE>(readHandle);
        m_outputThread = std::thread([this, handle]() {
            char buffer[4096];
            std::string carry;
            DWORD bytesRead = 0;
            while (m_running && ReadFile(handle, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
            {
                carry.append(buffer, bytesRead);
                std::size_t pos;
                while ((pos = carry.find('\n')) != std::string::npos)
                {
                    std::cerr << "[datasuite-r] " << carry.substr(0, pos) << std::endl;
                    carry.erase(0, pos + 1);
                }
            }
            if (!carry.empty())
            {
                std::cerr << "[datasuite-r] " << carry << std::endl;
            }
            CloseHandle(handle);
        });
#else
        int fd = static_cast<int>(reinterpret_cast<intptr_t>(readHandle));
        m_outputThread = std::thread([this, fd]() {
            char buffer[4096];
            std::string carry;
            ssize_t bytesRead;
            while (m_running && (bytesRead = read(fd, buffer, sizeof(buffer))) > 0)
            {
                carry.append(buffer, static_cast<std::size_t>(bytesRead));
                std::size_t pos;
                while ((pos = carry.find('\n')) != std::string::npos)
                {
                    std::cerr << "[datasuite-r] " << carry.substr(0, pos) << std::endl;
                    carry.erase(0, pos + 1);
                }
            }
            if (!carry.empty())
            {
                std::cerr << "[datasuite-r] " << carry << std::endl;
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
            throw std::runtime_error("Failed to create output pipe for datasuite-r process");
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
            throw std::runtime_error("Failed to initialize process attribute list for datasuite-r process");
        }
        HANDLE inheritList[] = { writeHandle };
        if (!UpdateProcThreadAttribute(
                attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inheritList, sizeof(inheritList), nullptr, nullptr))
        {
            DeleteProcThreadAttributeList(attrList);
            CloseHandle(writeHandle);
            CloseHandle(readHandle);
            throw std::runtime_error("Failed to set inherited handle list for datasuite-r process");
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
            throw std::runtime_error("Failed to spawn datasuite-r process (CreateProcess failed)");
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
            throw std::runtime_error("Failed to create output pipe for datasuite-r process");
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            close(pipeFds[0]);
            close(pipeFds[1]);
            throw std::runtime_error("Failed to fork datasuite-r process");
        }
        if (pid == 0)
        {
            close(pipeFds[0]);
            dup2(pipeFds[1], STDOUT_FILENO);
            dup2(pipeFds[1], STDERR_FILENO);
            close(pipeFds[1]);
            execv(m_options.kernelExePath.c_str(), argv.data());
            _exit(127);
        }

        close(pipeFds[1]);
        m_processId = pid;
        m_stdoutFd = pipeFds[0];
        startOutputPump(reinterpret_cast<void*>(static_cast<intptr_t>(m_stdoutFd)));
    }

    bool KernelProcess::isAlive() const
    {
        if (m_processId <= 0)
        {
            return false;
        }
        int status = 0;
        pid_t result = waitpid(m_processId, &status, WNOHANG);
        return result == 0;
    }

    void KernelProcess::kill()
    {
        m_running = false;
        if (m_processId > 0)
        {
            ::kill(m_processId, SIGKILL);
            int status = 0;
            waitpid(m_processId, &status, 0);
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
        int status = 0;
        pid_t result = waitpid(m_processId, &status, WNOHANG | WUNTRACED);
        if (result == 0)
        {
            return "process is still running -- likely hung or blocked rather than crashed";
        }
        if (result < 0)
        {
            return "unable to query process status (waitpid failed)";
        }
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
