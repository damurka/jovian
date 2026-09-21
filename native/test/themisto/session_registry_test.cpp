// Exercises SessionRegistry (native/src/themisto/session_registry.cpp)
// directly, bypassing HttpApi/WsRelay -- this is the piece the
// implementation plan called for testing "against a fake ClientZmq/kernel
// where feasible" and that never got covered. A fake ClientZmq would need
// SessionRegistry to accept an injectable client/process factory, which it
// doesn't (and refactoring it to support that is a bigger, riskier change
// than this test warrants); using the real elara executable this test
// binary is built alongside is a smaller, higher-fidelity alternative that
// still isolates SessionRegistry from HTTP/WS (already covered by
// test/integration/session-manager.test.ts on the TypeScript side).
//
// Needs a working R installation to actually start a kernel -- skips itself
// (GTEST_SKIP) rather than failing when one isn't found, matching the
// skip-if-native-binary-missing pattern already used by the TS test suite.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "adrastea/json.hpp"
#include "themisto/session_registry.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/sysctl.h>
#else
#include <dirent.h>
#endif

using namespace adrastea;
using namespace themisto;

namespace
{
    // R_HOME env var first (lets a developer/CI override), then the R
    // installation CMake itself already found and validated at configure
    // time (find_package(R REQUIRED) in the root CMakeLists.txt, propagated
    // here as ELARA_TEST_R_HOME) -- not a path guessed at from one
    // machine. Neither present means no usable fallback; SetUp() skips the
    // test rather than pointing R_HOME at somewhere that doesn't exist.
    std::string resolveRHome()
    {
        if (const char* fromEnv = std::getenv("R_HOME"))
        {
            return fromEnv;
        }
#ifdef ELARA_TEST_R_HOME
        return ELARA_TEST_R_HOME;
#else
        return "";
#endif
    }

    // Polls `predicate` until it returns true or `timeoutMs` elapses.
    // Returns whether it succeeded -- callers assert on the result rather
    // than this helper throwing, so a timeout shows up as a normal EXPECT
    // failure with a useful message instead of an opaque hang.
    bool waitFor(const std::function<bool()>& predicate, int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return predicate();
    }

    // Counts currently-running processes with the given (bare, no path,
    // no extension) executable name -- used to verify a concurrency fix
    // directly at the OS-process level, since a leaked shared_ptr<Session>
    // whose kernel process never gets a matching KernelProcess::kill() call
    // wouldn't otherwise show up as a C++-level assertion failure of any
    // kind. `exeName` is platform-bare ("elara", never "elara.exe") --
    // callers add the .exe suffix themselves on Windows (see
    // kElaraProcessName below), matching how each platform actually spells
    // the name in its own process listing.
#ifdef _WIN32
    // Explicitly the *W (wide) API regardless of this target's own UNICODE
    // setting, so szExeFile's element type isn't ambiguous -- PROCESSENTRY32/
    // Process32First(Next) unqualified are UNICODE-conditional macros that
    // could just as easily resolve back to these same *W versions anyway,
    // silently mismatching a std::string comparison. exeName is converted
    // to wide here instead, keeping the public signature portable.
    int countProcessesNamed(const std::string& exeName)
    {
        std::wstring wideExeName(exeName.begin(), exeName.end()); // ASCII-only process names, so this widening is exact

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return -1;
        }
        int count = 0;
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (wideExeName == entry.szExeFile)
                {
                    ++count;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return count;
    }
#elif defined(__APPLE__)
    int countProcessesNamed(const std::string& exeName)
    {
        int bufferSize = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (bufferSize <= 0)
        {
            return -1;
        }
        std::vector<pid_t> pids(bufferSize / sizeof(pid_t) + 1);
        int actualSize = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
        if (actualSize <= 0)
        {
            return -1;
        }
        int numPids = actualSize / sizeof(pid_t);

        int count = 0;
        char pathBuffer[PROC_PIDPATHINFO_MAXSIZE];
        for (int i = 0; i < numPids; ++i)
        {
            if (pids[i] <= 0)
            {
                continue;
            }
            int pathLen = proc_pidpath(pids[i], pathBuffer, sizeof(pathBuffer));
            if (pathLen <= 0)
            {
                continue; // e.g. a process that exited between listing and querying, or one we can't see
            }
            std::string path(pathBuffer, pathLen);
            std::size_t slash = path.find_last_of('/');
            std::string baseName = (slash == std::string::npos) ? path : path.substr(slash + 1);
            if (baseName == exeName)
            {
                ++count;
            }
        }
        return count;
    }
#else
    // Linux: /proc/<pid>/comm holds just the base executable name (no path,
    // trailing newline) -- reliable here since "elara"/"themisto" are well
    // under its historical 15-character truncation limit.
    int countProcessesNamed(const std::string& exeName)
    {
        DIR* procDir = opendir("/proc");
        if (!procDir)
        {
            return -1;
        }
        int count = 0;
        struct dirent* entry;
        while ((entry = readdir(procDir)) != nullptr)
        {
            const std::string pid = entry->d_name;
            if (pid.empty() || !std::all_of(pid.begin(), pid.end(), [](unsigned char c) { return std::isdigit(c); }))
            {
                continue;
            }
            std::ifstream commFile("/proc/" + pid + "/comm");
            if (!commFile)
            {
                continue; // process exited between the readdir() and this open, most likely
            }
            std::string name;
            std::getline(commFile, name);
            if (name == exeName)
            {
                ++count;
            }
        }
        closedir(procDir);
        return count;
    }
#endif

    const std::string kElaraProcessName =
#ifdef _WIN32
        "elara.exe";
#else
        "elara";
#endif

    class SessionRegistryTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            std::string rHome = resolveRHome();
            if (rHome.empty() || !std::filesystem::exists(rHome))
            {
                GTEST_SKIP() << "No R installation found (checked R_HOME env var and the default dev path) -- "
                                "skipping, since SessionRegistry::createSession() needs a real R to register.";
            }
#ifndef ELARA_TEST_KERNEL_EXE
            GTEST_SKIP() << "ELARA_TEST_KERNEL_EXE not defined by CMake -- elara target not built alongside tests.";
#else
            if (!std::filesystem::exists(ELARA_TEST_KERNEL_EXE))
            {
                GTEST_SKIP() << "elara executable not found at " ELARA_TEST_KERNEL_EXE;
            }
            m_rHome = rHome;
            // Deliberately leaked (not a unique_ptr): ~SessionRegistry()'s
            // implicit member teardown -- specifically something downstream
            // of the registration listener's ZMQ context/socket -- was
            // confirmed (via native/test/plain_diag.cpp, not part of this
            // suite) to hang on process exit even though every explicit
            // SessionRegistry/Session call involved returns normally.
            // Production code never hits this either way: themisto-
            // supervisor's own process is always force-killed by its parent
            // (SupervisorClient.kill() in lib/session/supervisor-client.ts),
            // never gracefully destructed. Leaking here and relying on
            // std::_Exit() (this file's main(), below) to reclaim the
            // process's resources sidesteps a destructor that's confirmed
            // broken rather than papering over it with a guess.
            m_registry = new SessionRegistry({ { "r", ELARA_TEST_KERNEL_EXE } }, "127.0.0.1");
            m_registry->startRegistrationListener();
#endif
        }

        std::string m_rHome;
        SessionRegistry* m_registry = nullptr;
    };

    // 90s: generous on purpose. A real R startup (Rf_initEmbeddedR + loading
    // the bundled 'hera' package) plus the registration handshake can take
    // several seconds on a cold filesystem cache; this only needs to bound
    // the case where something is actually broken (e.g. the known
    // waitForConfiguration() no-timeout limitation noted in
    // session_registry.cpp), not the happy path's normal latency.
    constexpr int kTimeoutMs = 90000;

    // Captures everything a session relays (the same JSON envelopes ws_relay
    // forwards to a client) plus any kernelExit, so tests can wait on and
    // inspect specific replies by msg_type + parent_msg_id.
    class Collector
    {
    public:
        explicit Collector(const std::shared_ptr<Session>& session)
        {
            std::lock_guard<std::mutex> lock(session->callbackMutex);
            session->onMessage = [this](const std::string& text) {
                std::lock_guard<std::mutex> lock2(m_mutex);
                m_messages.push_back(json::parse(text));
            };
            session->onKernelExit = [this](const std::string& reason) {
                std::lock_guard<std::mutex> lock2(m_mutex);
                m_kernelExits.push_back(reason);
            };
        }

        std::optional<json> find(const std::string& msgType, const std::string& parentMsgId)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& msg : m_messages)
            {
                if (msg.value("msg_type", "") == msgType && msg.value("parent_msg_id", "") == parentMsgId)
                {
                    std::optional<json> found;
                    found.emplace(msg);
                    return found;
                }
            }
            return std::nullopt;
        }

        // Waits for a stream message of `parentMsgId` whose text contains `needle`.
        bool waitForStreamText(const std::string& needle, const std::string& parentMsgId)
        {
            return waitFor([&]() {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (const auto& msg : m_messages)
                {
                    if (msg.value("msg_type", "") == "stream" && msg.value("parent_msg_id", "") == parentMsgId &&
                        msg.at("content").value("text", "").find(needle) != std::string::npos)
                    {
                        return true;
                    }
                }
                return false;
            }, kTimeoutMs);
        }

        std::optional<json> waitForMessage(const std::string& msgType, const std::string& parentMsgId)
        {
            waitFor([&]() { return find(msgType, parentMsgId).has_value(); }, kTimeoutMs);
            return find(msgType, parentMsgId);
        }

        std::vector<json> messages()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_messages;
        }

        std::vector<std::string> kernelExits()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_kernelExits;
        }

    private:
        std::mutex m_mutex;
        std::vector<json> m_messages;
        std::vector<std::string> m_kernelExits;
    };
}

// Doesn't need R or a real kernel process at all -- getSession()/
// listSessions()/stopSession() on a registry that never had a session
// created are pure state queries against an empty m_sessions map. Plain
// TEST() rather than TEST_F(SessionRegistryTest, ...) deliberately: sharing
// that fixture would gate these on an R installation they don't need,
// exactly the kind of environment dependency this covers avoiding.
// Deliberately leaked for the same reason as SessionRegistryTest's fixture
// member (see its comment above) -- consistent behavior, not just a
// borrowed pattern.
TEST(SessionRegistryEmptyStateTest, GetSessionOnUnknownIdReturnsNull)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_EQ(registry->getSession("does-not-exist"), nullptr);
}

TEST(SessionRegistryEmptyStateTest, ListSessionsOnEmptyRegistryReturnsEmptyArray)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    json sessions = registry->listSessions();
    EXPECT_TRUE(sessions.is_array());
    EXPECT_TRUE(sessions.empty());
}

TEST(SessionRegistryEmptyStateTest, StopSessionOnUnknownIdReturnsFalse)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_FALSE(registry->stopSession("does-not-exist"));
}

TEST(SessionRegistryEmptyStateTest, SendExecuteOnUnknownIdReturnsFalse)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_FALSE(registry->sendExecute("does-not-exist", "msg-1", "1 + 1", json::object()));
}

TEST(SessionRegistryEmptyStateTest, SendInterruptOnUnknownIdReturnsFalse)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_FALSE(registry->sendInterrupt("does-not-exist", "msg-1"));
}

TEST(SessionRegistryEmptyStateTest, RestartSessionOnUnknownIdReturnsErrorAndEmptyId)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    std::string error;
    std::string id = registry->restartSession("does-not-exist", error);

    EXPECT_TRUE(id.empty());
    EXPECT_FALSE(error.empty());
}

TEST(SessionRegistryEmptyStateTest, CreateSessionSurfacesAKernelSpawnFailureAsAnError)
{
    // No R installation needed: a nonexistent kernel exe path fails inside
    // KernelProcess::start() (CreateProcessA) before anything R-related
    // happens, hitting createSessionWithId()'s catch block
    // (session_registry.cpp) rather than the registration-handshake path.
    auto* registry = new SessionRegistry({ { "r", "C:\\this\\path\\does\\not\\exist\\elara.exe" } }, "127.0.0.1");
    registry->startRegistrationListener();

    SessionOptions options;
    std::string error;
    std::string id = registry->createSession(options, error);

    EXPECT_TRUE(id.empty());
    EXPECT_FALSE(error.empty());
}

// Deliberately leaked for the same reason as SessionRegistryTest's fixture
// member (see its comment above) -- consistent behavior, not just a
// borrowed pattern.
TEST(SessionRegistryEmptyStateTest, CreateSessionFailsCleanlyForAnUnregisteredKernelType)
{
    // This supervisor only knows about "r" (e.g. carpo wasn't built --
    // JOVIAN_BUILD_CARPO defaults OFF, see main.cpp) -- requesting
    // kernelType "python" must fail with a clear, immediate error instead
    // of crashing, hanging, or silently falling back to "r"'s executable.
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    SessionOptions options;
    options.kernelType = "python";
    std::string error;
    std::string id = registry->createSession(options, error);

    EXPECT_TRUE(id.empty());
    EXPECT_NE(error.find("python"), std::string::npos);
}

// ElaraExitsCleanlyWithAnActionableMessageWhenRCannotBeLoaded moved to
// native/test/elara/elara_test.cpp -- it tests elara.exe's own dynamic R
// loading directly (spawned raw, bypassing SessionRegistry entirely), not
// anything SessionRegistry does, so it belongs with elara's own tests now
// that native/test/ is split per feature (adrastea/elara/themisto).

TEST(SessionRegistryEmptyStateTest, CreateSessionFailsFastWhenTheKernelProcessDiesBeforeRegistering)
{
#ifndef ELARA_TEST_KERNEL_EXE
    GTEST_SKIP() << "ELARA_TEST_KERNEL_EXE not defined by CMake -- elara target not built alongside tests.";
#else
    if (!std::filesystem::exists(ELARA_TEST_KERNEL_EXE))
    {
        GTEST_SKIP() << "elara executable not found at " ELARA_TEST_KERNEL_EXE;
    }

    // Regression test for a real, reproduced bug: ClientHandshakeZmqImpl::
    // waitForConfiguration() (client_handshake_zmq.cpp) used to block on a
    // single long-timeout recv with no awareness of the kernel process it
    // was waiting on -- a real createSession() with no R_HOME configured
    // never returned (elara.exe itself exits in well under 100ms, but the
    // registration wait didn't know that and sat for the full, then-only,
    // 60s timeout regardless), leaving a live but permanently-stuck
    // themisto.exe behind. Fixed by polling the spawned KernelProcess's own
    // isAlive() between short-interval recv attempts instead of one long
    // blocking one -- this asserts the *fast* path specifically (a bounded
    // time well under the 60s ceiling), not just "eventually fails".
    //
    // No R installation needed here (unlike SessionRegistryTest's fixture):
    // this doesn't need R to be present, it needs it to be absent/
    // misconfigured -- an empty rHome is enough to reproduce elara's own
    // fast-exit path regardless of what's actually installed on the machine
    // running this test.
    auto* registry = new SessionRegistry({ { "r", ELARA_TEST_KERNEL_EXE } }, "127.0.0.1");
    registry->startRegistrationListener();

    SessionOptions options; // rHome left empty deliberately
    std::string error;

    auto start = std::chrono::steady_clock::now();
    std::string id = registry->createSession(options, error);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(id.empty());
    EXPECT_FALSE(error.empty());
    // 30s, not a tighter bound closer to the poll loop's actual ~250ms
    // interval: a real Windows CI run hit 13.9s here once (process-spawn
    // overhead on a loaded runner, not a regression -- the poll loop
    // itself is unaffected), which a 10s bound flagged as a failure. 30s
    // still catches the actual regression this guards against (the old
    // behavior waited the full 60s ceiling every time), with real margin
    // for a slow runner instead of a tight one that flakes under load.
    EXPECT_LT(elapsed, std::chrono::seconds(30))
        << "createSession() took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        << "ms -- expected the dead-process fast path to fire well under the 60s registration timeout";
#endif
}

TEST(SessionStructTest, ToStringCoversEveryStatusIncludingUnknown)
{
    EXPECT_EQ(toString(SessionStatus::Starting), "starting");
    EXPECT_EQ(toString(SessionStatus::Ready), "ready");
    EXPECT_EQ(toString(SessionStatus::Stopped), "stopped");
    EXPECT_EQ(toString(SessionStatus::Crashed), "crashed");
    EXPECT_EQ(toString(static_cast<SessionStatus>(999)), "unknown");
}

TEST(SessionStructTest, EmitKernelExitInvokesTheRegisteredCallback)
{
    Session session;
    bool invoked = false;
    std::string receivedReason;
    {
        std::lock_guard<std::mutex> lock(session.callbackMutex);
        session.onKernelExit = [&](const std::string& reason) {
            invoked = true;
            receivedReason = reason;
        };
    }

    session.emitKernelExit("process exited with code 0x1");

    EXPECT_TRUE(invoked);
    EXPECT_EQ(receivedReason, "process exited with code 0x1");
}

TEST(SessionStructTest, EmitKernelExitWithNoCallbackRegisteredIsANoOp)
{
    Session session;
    EXPECT_NO_THROW(session.emitKernelExit("reason"));
}

TEST(SessionStructTest, DestructorJoinsAStillRunningPollThread)
{
    // Mirrors the shape SessionRegistry::startPolling() sets up (polling +
    // a thread that loops on it), without needing a real client/kernel --
    // Session::~Session() (session_registry.cpp) just needs polling=true
    // and a joinable thread to exercise its join() branch.
    auto session = std::make_unique<Session>();
    // Captures the raw Session*, not the unique_ptr by reference: reset()
    // nulls out the unique_ptr's stored pointer *before* ~Session() runs
    // (which is what actually joins this thread), so a lambda reading
    // through the unique_ptr itself would dereference null mid-join.
    Session* raw = session.get();
    raw->polling = true;
    raw->pollThread = std::thread([raw]() {
        while (raw->polling)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    EXPECT_NO_THROW(session.reset());
}

TEST_F(SessionRegistryTest, CreateSessionRegistersAndTracksARealKernel)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);

    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(session->status.load(), SessionStatus::Ready);

    json sessions = m_registry->listSessions();
    bool found = false;
    for (const auto& entry : sessions)
    {
        if (entry.at("sessionId").get<std::string>() == id)
        {
            found = true;
            EXPECT_EQ(entry.at("status").get<std::string>(), "ready");
        }
    }
    EXPECT_TRUE(found) << "created session did not show up in listSessions()";

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, ExecuteRoundTripsThroughTheRealKernel)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);

    std::vector<json> received;
    std::mutex receivedMutex;
    {
        std::lock_guard<std::mutex> lock(session->callbackMutex);
        session->onMessage = [&](const std::string& text) {
            std::lock_guard<std::mutex> lock2(receivedMutex);
            received.push_back(json::parse(text));
        };
    }

    const std::string msgId = "test-exec-1";
    ASSERT_TRUE(m_registry->sendExecute(id, msgId, "1 + 1", json::object()));

    bool gotReply = waitFor([&]() {
        std::lock_guard<std::mutex> lock(receivedMutex);
        for (const auto& msg : received)
        {
            if (msg.value("msg_type", "") == "execute_reply" && msg.value("parent_msg_id", "") == msgId)
            {
                return true;
            }
        }
        return false;
    }, kTimeoutMs);

    ASSERT_TRUE(gotReply) << "never received an execute_reply for " << msgId;

    {
        std::lock_guard<std::mutex> lock(receivedMutex);
        bool sawResult = false;
        for (const auto& msg : received)
        {
            if (msg.value("msg_type", "") == "execute_result")
            {
                sawResult = true;
                auto textPlain = msg.at("content").at("data").value("text/plain", std::string());
                EXPECT_NE(textPlain.find('2'), std::string::npos) << "unexpected execute_result: " << textPlain;
            }
        }
        EXPECT_TRUE(sawResult) << "never received an execute_result for 1 + 1";
    }

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, SendHistoryReturnsAGenuineHistoryReplyForRealExecutions)
{
    // Regression test for a real gap: KernelCore::historyRequest()
    // (kernel_core.cpp) and HistoryManager (adrastea/history_manager.hpp)
    // were always fully implemented on the kernel side, but nothing outside
    // a raw Jupyter client connecting directly to the kernel's own ZMQ
    // ports could ever reach it -- ws_relay.cpp had no "history" frame case
    // at all. This exercises the real end-to-end path this fixes: a
    // history_request sent the same way sendExecute()/sendInterrupt()
    // already are, answered by the same real kernel that ran the code.
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);

    std::vector<json> received;
    std::mutex receivedMutex;
    {
        std::lock_guard<std::mutex> lock(session->callbackMutex);
        session->onMessage = [&](const std::string& text) {
            std::lock_guard<std::mutex> lock2(receivedMutex);
            received.push_back(json::parse(text));
        };
    }

    ASSERT_TRUE(m_registry->sendExecute(id, "hist-exec-1", "42", json::object()));
    ASSERT_TRUE(waitFor([&]() {
        std::lock_guard<std::mutex> lock(receivedMutex);
        return std::any_of(received.begin(), received.end(), [](const json& msg) {
            return msg.value("msg_type", "") == "execute_reply" && msg.value("parent_msg_id", "") == "hist-exec-1";
        });
    }, kTimeoutMs)) << "never received an execute_reply for the setup execution";

    const std::string histMsgId = "hist-req-1";
    ASSERT_TRUE(m_registry->sendHistory(id, histMsgId, json::object()));

    bool gotHistoryReply = waitFor([&]() {
        std::lock_guard<std::mutex> lock(receivedMutex);
        return std::any_of(received.begin(), received.end(), [&](const json& msg) {
            return msg.value("msg_type", "") == "history_reply" && msg.value("parent_msg_id", "") == histMsgId;
        });
    }, kTimeoutMs);
    ASSERT_TRUE(gotHistoryReply) << "never received a history_reply for " << histMsgId;

    {
        std::lock_guard<std::mutex> lock(receivedMutex);
        auto it = std::find_if(received.begin(), received.end(), [&](const json& msg) {
                return msg.value("msg_type", "") == "history_reply" && msg.value("parent_msg_id", "") == histMsgId;
        });
        ASSERT_NE(it, received.end());
        EXPECT_EQ(it->at("content").value("status", ""), "ok");
        const auto& history = it->at("content").at("history");
        ASSERT_FALSE(history.empty()) << "expected at least the '42' execution to show up in history";
        // Each short entry is [session, line_num, input] -- confirm the actual
        // code we ran is really in there, not just that *something* came back.
        bool foundOurExecution = std::any_of(history.begin(), history.end(), [](const json& entry) {
            return entry.at(2).get<std::string>() == "42";
        });
        EXPECT_TRUE(foundOurExecution) << "history_reply did not contain the '42' execution: " << it->at("content").dump();
    }

    // Not while holding receivedMutex: stopSession() keeps the poll thread
    // running to relay the kernel's shutdown_reply, and that thread's
    // onMessage callback needs this same mutex.
    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, StopSessionMarksItStoppedAndTerminatesTheProcess)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    // Grabbed *before* stopping -- a stopped session is now released from
    // the registry (see stopSession()'s own comment on why: unlike Crashed,
    // it's permanently terminal, so there's no reason to keep its ZMQ
    // context/ClientZmq/KernelProcess alive for the rest of this themisto
    // process's lifetime). This local shared_ptr keeps the object itself
    // alive long enough to still assert its final status directly, the
    // same safe "still-held copy outlives the registry's own reference"
    // guarantee stopSession()'s comment describes.
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);

    EXPECT_TRUE(m_registry->stopSession(id));

    EXPECT_EQ(session->status.load(), SessionStatus::Stopped);
    EXPECT_EQ(m_registry->getSession(id), nullptr)
        << "a stopped session should be released from the registry, not kept around forever";
}

TEST_F(SessionRegistryTest, StopSessionReleasesTheSessionButNotItsOperationLock)
{
    // Companion to the test above -- specifically distinguishes what gets
    // released (the Session object/map entry) from what deliberately never
    // does (the per-id operation lock, session_registry.hpp's own
    // m_sessionOperationLocks comment): a second stopSession() call for the
    // same, already-erased id must still return false cleanly through the
    // exact same per-id lock machinery, not crash or hang by trying to
    // create/acquire a lock for an id whose Session is already gone.
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    EXPECT_TRUE(m_registry->stopSession(id));
    ASSERT_EQ(m_registry->getSession(id), nullptr);

    EXPECT_FALSE(m_registry->stopSession(id)) << "stopping an already-released session should fail cleanly";
}

TEST_F(SessionRegistryTest, PollLoopDetectsAnExternallyKilledKernelProcessQuickly)
{
    // Regression test for a real, user-reported bug: killing a kernel
    // process externally (Task Manager, a segfault -- anything that isn't
    // this codebase's own graceful stopSession()) used to go completely
    // undetected until the ZMQ heartbeat's worst-case timeout elapsed (4
    // failed round trips x 20s = up to ~80s -- ClientHeartbeat/
    // ClientZmqImpl's max_retry/heartbeat_timeout), during which every
    // execute() against that session just hung until ITS OWN unrelated
    // 30s client-side timeout fired first. pollLoop() (session_registry.cpp)
    // now polls the OS process handle directly on every iteration
    // (~5ms), so an externally killed process is caught almost
    // immediately -- asserted here with a 2s bound, nowhere close to the
    // 60-80s heartbeat ceiling this used to require.
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    ASSERT_TRUE(session->process != nullptr);

    std::string exitReason;
    {
        std::lock_guard<std::mutex> lock(session->callbackMutex);
        session->onKernelExit = [&](const std::string& reason) { exitReason = reason; };
    }

    // Not stopSession()/kill() through the registry -- that's the graceful
    // path this test deliberately bypasses. KernelProcess::kill() itself
    // (TerminateProcess on Windows) is the same OS-level effect an external
    // Task Manager kill has: the process is simply gone, with none of
    // SessionRegistry's own graceful-shutdown bookkeeping involved.
    session->process->kill();

    bool detected = waitFor([&]() {
        return session->status.load() == SessionStatus::Crashed;
    }, 2000);

    EXPECT_TRUE(detected) << "pollLoop() did not detect the killed process within 2s";
    EXPECT_FALSE(exitReason.empty()) << "onKernelExit was never invoked";

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, SendInterruptOnARealSessionReturnsTrue)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    EXPECT_TRUE(m_registry->sendInterrupt(id, "interrupt-1"));

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, RestartSessionReplacesTheKernelButKeepsTheSameId)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    std::string restartError;
    std::string restartedId = m_registry->restartSession(id, restartError);

    ASSERT_FALSE(restartedId.empty()) << "restartSession failed: " << restartError;
    EXPECT_EQ(restartedId, id);

    auto session = m_registry->getSession(restartedId);
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(session->status.load(), SessionStatus::Ready);

    // Prove the restarted session has a genuinely working (not stale) kernel
    // behind it, not just a status flag flipped back to Ready.
    std::vector<json> received;
    std::mutex receivedMutex;
    {
        std::lock_guard<std::mutex> lock(session->callbackMutex);
        session->onMessage = [&](const std::string& text) {
            std::lock_guard<std::mutex> lock2(receivedMutex);
            received.push_back(json::parse(text));
        };
    }

    const std::string msgId = "test-restart-exec";
    ASSERT_TRUE(m_registry->sendExecute(restartedId, msgId, "1 + 1", json::object()));

    bool gotReply = waitFor([&]() {
        std::lock_guard<std::mutex> lock(receivedMutex);
        for (const auto& msg : received)
        {
            if (msg.value("msg_type", "") == "execute_reply" && msg.value("parent_msg_id", "") == msgId)
            {
                return true;
            }
        }
        return false;
    }, kTimeoutMs);
    ASSERT_TRUE(gotReply) << "restarted session never replied to an execute_request";

    m_registry->stopSession(restartedId);
}

TEST_F(SessionRegistryTest, ConcurrentRestartsForTheSameSessionDontLeakAnExtraKernelProcess)
{
    // Regression test for a real, reproduced bug: two overlapping
    // restartSession() calls for the same id used to race -- each
    // independently stopped the old kernel and spawned its own new one
    // with no serialization between them, leaking whichever one's kernel
    // process lost the race to overwrite m_sessions[id] and corrupting the
    // loser's HTTP response ("Unexpected end of JSON input" on the client
    // side). Found via a live VS Code repro: clicking "Restart" again
    // before the previous click's ~2-5s cycle finished left extra
    // elara.exe processes running -- confirmed via real OS process
    // counts, not just inferred from the C++ call graph, so this asserts
    // the same way rather than on some indirect proxy (e.g. call timing,
    // which a blocked-and-waiting second caller would confound anyway).
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    ASSERT_TRUE(waitFor([&]() { return countProcessesNamed(kElaraProcessName) == 1; }, kTimeoutMs))
        << "expected exactly one " << kElaraProcessName << " after the initial createSession";

    bool ok1 = false;
    bool ok2 = false;
    std::thread t1([&]() {
        std::string restartError;
        ok1 = !m_registry->restartSession(id, restartError).empty();
    });
    std::thread t2([&]() {
        std::string restartError;
        ok2 = !m_registry->restartSession(id, restartError).empty();
    });
    t1.join();
    t2.join();

    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(session->status.load(), SessionStatus::Ready);

    // The only assertion that actually matters here: exactly one live
    // kernel process backs this one session, never two.
    EXPECT_TRUE(waitFor([&]() { return countProcessesNamed(kElaraProcessName) == 1; }, kTimeoutMs))
        << "expected exactly one " << kElaraProcessName << " after two concurrent restarts, found "
        << countProcessesNamed(kElaraProcessName);

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, ConcurrentExecuteDuringARestartDoesNotCrashOrLeak)
{
    // sendExecute()/sendInterrupt() used to look up a session and call
    // straight into its ClientZmq with no serialization against a
    // concurrent stopSession()/restartSession() for that same id -- a
    // second thread's stopChannels()/kill() could be tearing that exact
    // client down at the same moment. Fixed by having them take the same
    // per-id lock restartSession()/stopSession() hold. This fires a burst
    // of execute requests concurrently with a restart of the same session
    // and asserts on the same real, external signal as the restart-leak
    // test above: it doesn't crash, doesn't hang, and doesn't leak a
    // kernel process -- not just "the C++ call graph looks fine".
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    std::atomic<bool> keepSendingExecute{ true };
    std::thread executeThread([&]() {
        int counter = 0;
        while (keepSendingExecute)
        {
            m_registry->sendExecute(id, "burst-" + std::to_string(counter++), "1 + 1", json::object());
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    std::string restartError;
    std::string restartedId = m_registry->restartSession(id, restartError);

    keepSendingExecute = false;
    executeThread.join();

    ASSERT_FALSE(restartedId.empty()) << "restartSession failed: " << restartError;
    EXPECT_EQ(restartedId, id);

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(session->status.load(), SessionStatus::Ready);

    EXPECT_TRUE(waitFor([&]() { return countProcessesNamed(kElaraProcessName) == 1; }, kTimeoutMs))
        << "expected exactly one " << kElaraProcessName << " after a restart racing concurrent execute() calls, found "
        << countProcessesNamed(kElaraProcessName);

    // Prove the post-restart kernel is genuinely usable, not just "still
    // has a process" -- a real execute/reply round trip.
    std::vector<json> received;
    std::mutex receivedMutex;
    {
        std::lock_guard<std::mutex> lock(session->callbackMutex);
        session->onMessage = [&](const std::string& text) {
            std::lock_guard<std::mutex> lock2(receivedMutex);
            received.push_back(json::parse(text));
        };
    }
    const std::string msgId = "post-race-exec";
    ASSERT_TRUE(m_registry->sendExecute(id, msgId, "1 + 1", json::object()));
    bool gotReply = waitFor([&]() {
        std::lock_guard<std::mutex> lock(receivedMutex);
        for (const auto& msg : received)
        {
            if (msg.value("msg_type", "") == "execute_reply" && msg.value("parent_msg_id", "") == msgId)
            {
                return true;
            }
        }
        return false;
    }, kTimeoutMs);
    EXPECT_TRUE(gotReply) << "session never replied to an execute_request after the race";

    m_registry->stopSession(id);
}

TEST(SessionRegistryEmptyStateTest, SendRequestOnlyAllowsTheDocumentedRequestTypesOnTheirOwnChannels)
{
    auto* registry = new SessionRegistry({ { "r", "unused-kernel-exe-path" } }, "127.0.0.1");
    registry->startRegistrationListener();

    // Rejected on the whitelist alone, before any session lookup.
    struct Case { const char* channel; const char* msgType; };
    for (const Case& c : { Case{ "shell", "execute_request" },       // has its own entry point (stdin/history semantics)
                           Case{ "shell", "shutdown_request" },      // lifecycle: stopSession()/restartSession() only
                           Case{ "control", "shutdown_request" },
                           Case{ "shell", "interrupt_request" },     // control-channel request
                           Case{ "control", "complete_request" },    // shell-channel request
                           Case{ "stdin", "input_reply" },
                           Case{ "iopub", "status" },
                           Case{ "shell", "no_such_request" } })
    {
        std::string error;
        EXPECT_FALSE(registry->sendRequest("does-not-exist", c.channel, c.msgType, "m", json::object(), error))
            << c.channel << "/" << c.msgType;
        EXPECT_NE(error.find("not an allowed request"), std::string::npos) << c.channel << "/" << c.msgType << ": " << error;
    }

    // An allowed type for an unknown session fails for the right reason.
    std::string error;
    EXPECT_FALSE(registry->sendRequest("does-not-exist", "shell", "kernel_info_request", "m", json::object(), error));
    EXPECT_EQ(error, "session not found");
}

TEST_F(SessionRegistryTest, SendRequestRoundTripsKernelInfoCompleteInspectIsCompleteAndCommInfo)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    auto request = [&](const std::string& msgType, const std::string& msgId, const json& content) {
        std::string requestError;
        EXPECT_TRUE(m_registry->sendRequest(id, "shell", msgType, msgId, content, requestError)) << requestError;
    };

    request("kernel_info_request", "ki", json::object());
    request("complete_request", "co", { { "code", "pri" }, { "cursor_pos", 3 } });
    request("inspect_request", "in", { { "code", "mean" }, { "cursor_pos", 4 }, { "detail_level", 0 } });
    request("is_complete_request", "ic-open", { { "code", "1 +" } });
    request("is_complete_request", "ic-done", { { "code", "1 + 1" } });
    request("comm_info_request", "ci", { { "target_name", "no_such_target" } });

    auto info = collector.waitForMessage("kernel_info_reply", "ki");
    ASSERT_TRUE(info.has_value()) << "no kernel_info_reply";
    EXPECT_EQ(info->at("channel"), "shell");
    EXPECT_EQ(info->at("content").at("status"), "ok");
    EXPECT_EQ(info->at("content").at("language_info").at("name"), "R");
    EXPECT_TRUE(info->at("content").contains("protocol_version"));

    auto complete = collector.waitForMessage("complete_reply", "co");
    ASSERT_TRUE(complete.has_value()) << "no complete_reply";
    EXPECT_EQ(complete->at("content").at("status"), "ok");
    const auto& matches = complete->at("content").at("matches");
    EXPECT_TRUE(std::find(matches.begin(), matches.end(), "print") != matches.end())
        << "completing 'pri' should offer print: " << matches.dump();

    auto inspect = collector.waitForMessage("inspect_reply", "in");
    ASSERT_TRUE(inspect.has_value()) << "no inspect_reply";
    EXPECT_EQ(inspect->at("content").at("status"), "ok");

    auto incomplete = collector.waitForMessage("is_complete_reply", "ic-open");
    ASSERT_TRUE(incomplete.has_value()) << "no is_complete_reply";
    EXPECT_EQ(incomplete->at("content").at("status"), "incomplete");
    auto complete2 = collector.waitForMessage("is_complete_reply", "ic-done");
    ASSERT_TRUE(complete2.has_value());
    EXPECT_EQ(complete2->at("content").at("status"), "complete");

    auto commInfo = collector.waitForMessage("comm_info_reply", "ci");
    ASSERT_TRUE(commInfo.has_value()) << "no comm_info_reply";
    EXPECT_EQ(commInfo->at("content").at("status"), "ok");
    EXPECT_TRUE(commInfo->at("content").at("comms").empty());

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, CommOpenForAnUnknownTargetIsAnsweredWithACommCloseOnIopub)
{
    // Spec: a kernel that has no handler for comm_open's target_name replies
    // with a comm_close (on iopub) carrying the same comm_id.
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendRequest(id, "shell", "comm_open", "co-1",
        { { "comm_id", "comm-abc" }, { "target_name", "no_such_target" }, { "data", json::object() } }, error)) << error;

    auto closed = collector.waitForMessage("comm_close", "co-1");
    ASSERT_TRUE(closed.has_value()) << "no comm_close for an unknown comm target";
    EXPECT_EQ(closed->at("channel"), "iopub");
    EXPECT_EQ(closed->at("content").at("comm_id"), "comm-abc");

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, InterruptRequestIsAnsweredWithAnInterruptReplyOnTheControlChannel)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendInterrupt(id, "int-1"));

    // Used to be silently dropped: nothing ever read the control channel.
    auto reply = collector.waitForMessage("interrupt_reply", "int-1");
    ASSERT_TRUE(reply.has_value()) << "no interrupt_reply relayed";
    EXPECT_EQ(reply->at("channel"), "control");
    EXPECT_EQ(reply->at("content").at("status"), "ok");

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, ExecuteForwardsUserExpressionsAndReportsEachOnesOwnFailure)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendExecute(id, "ue-1", "x <- 21",
        { { "userExpressions", { { "double", "x * 2" }, { "boom", "stop('kaboom')" } } } }));

    auto reply = collector.waitForMessage("execute_reply", "ue-1");
    ASSERT_TRUE(reply.has_value()) << "no execute_reply";
    ASSERT_EQ(reply->at("content").at("status"), "ok") << reply->dump();

    const auto& results = reply->at("content").at("user_expressions");
    ASSERT_TRUE(results.contains("double")) << results.dump();
    EXPECT_EQ(results.at("double").at("status"), "ok");
    EXPECT_NE(results.at("double").at("data").at("text/plain").get<std::string>().find("42"), std::string::npos)
        << results.dump();

    ASSERT_TRUE(results.contains("boom")) << results.dump();
    EXPECT_EQ(results.at("boom").at("status"), "error");
    EXPECT_NE(results.at("boom").at("evalue").get<std::string>().find("kaboom"), std::string::npos) << results.dump();

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, StopOnErrorAbortsRequestsAlreadyQueuedBehindTheFailure)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    // The first execution takes long enough (Sys.sleep) that the second and
    // third requests are certainly already queued when it fails.
    ASSERT_TRUE(m_registry->sendExecute(id, "soe-1", "Sys.sleep(1); stop('first fails')", { { "stopOnError", true } }));
    ASSERT_TRUE(m_registry->sendExecute(id, "soe-2", "1 + 1", { { "stopOnError", true } }));
    ASSERT_TRUE(m_registry->sendExecute(id, "soe-3", "2 + 2", { { "stopOnError", true } }));

    auto first = collector.waitForMessage("execute_reply", "soe-1");
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->at("content").at("status"), "error");

    auto second = collector.waitForMessage("execute_reply", "soe-2");
    auto third = collector.waitForMessage("execute_reply", "soe-3");
    ASSERT_TRUE(second.has_value() && third.has_value());
    EXPECT_EQ(second->at("content").at("status"), "aborted");
    EXPECT_EQ(third->at("content").at("status"), "aborted");

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, WithoutStopOnErrorAFailureDoesNotAbortTheQueue)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendExecute(id, "nsoe-1", "Sys.sleep(1); stop('first fails')", json::object()));
    ASSERT_TRUE(m_registry->sendExecute(id, "nsoe-2", "1 + 1", json::object()));

    auto second = collector.waitForMessage("execute_reply", "nsoe-2");
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->at("content").at("status"), "ok");

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, StopSessionSendsARealShutdownRequestAndRelaysItsReplyWithoutReportingACrash)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->stopSession(id));

    // The orderly, protocol-driven exit must not be reported as a crash...
    EXPECT_TRUE(collector.kernelExits().empty()) << "stop reported a spurious kernelExit: " << collector.kernelExits()[0];
    EXPECT_EQ(session->status.load(), SessionStatus::Stopped);

    // ...and the kernel's own shutdown_reply is relayed, restart=false.
    bool sawReply = false;
    for (const auto& msg : collector.messages())
    {
        if (msg.value("msg_type", "") == "shutdown_reply")
        {
            sawReply = true;
            EXPECT_EQ(msg.at("channel"), "control");
            EXPECT_EQ(msg.at("content").at("status"), "ok");
            EXPECT_EQ(msg.at("content").at("restart"), false);
        }
    }
    EXPECT_TRUE(sawReply) << "shutdown_reply was never relayed";
}

TEST_F(SessionRegistryTest, RestartSessionSendsShutdownRequestWithRestartTrue)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto oldSession = m_registry->getSession(id);
    ASSERT_TRUE(oldSession != nullptr);
    Collector collector(oldSession);

    std::string restartError;
    ASSERT_EQ(m_registry->restartSession(id, restartError), id) << restartError;

    EXPECT_TRUE(collector.kernelExits().empty()) << "restart reported a spurious kernelExit";

    bool sawRestartReply = false;
    for (const auto& msg : collector.messages())
    {
        if (msg.value("msg_type", "") == "shutdown_reply")
        {
            sawRestartReply = true;
            EXPECT_EQ(msg.at("content").at("restart"), true);
        }
    }
    EXPECT_TRUE(sawRestartReply) << "restart never relayed the kernel's shutdown_reply";

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, InterruptStopsARunningSleepAndTheKernelStaysUsable)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    const auto started = std::chrono::steady_clock::now();
    ASSERT_TRUE(m_registry->sendExecute(id, "sleep-1", "cat('sleeping\\n'); Sys.sleep(60)", json::object()));
    // Interrupt only once the code is really inside the sleep: an interrupt
    // that lands while hera is still setting up the evaluation is a
    // different (and racy) thing to test.
    ASSERT_TRUE(collector.waitForStreamText("sleeping", "sleep-1"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(m_registry->sendInterrupt(id, "int-sleep"));

    // The interrupt is answered while the sleep is still running (it used to
    // sit unread until the execution ended)...
    auto interruptReply = collector.waitForMessage("interrupt_reply", "int-sleep");
    ASSERT_TRUE(interruptReply.has_value()) << "no interrupt_reply while the execution was running";
    EXPECT_EQ(interruptReply->at("channel"), "control");

    // ...and the sleep itself ends promptly, not after its 60 seconds.
    auto executeReply = collector.waitForMessage("execute_reply", "sleep-1");
    ASSERT_TRUE(executeReply.has_value()) << "the interrupted execution never replied";
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 30)
        << "the sleep was not interrupted: " << executeReply->dump();
    EXPECT_NE(executeReply->at("content").at("status"), "ok") << executeReply->dump();

    // The kernel survived and still runs code.
    ASSERT_TRUE(m_registry->sendExecute(id, "after-1", "1 + 1", json::object()));
    auto after = collector.waitForMessage("execute_reply", "after-1");
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->at("content").at("status"), "ok");

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, InterruptStopsABusyLoop)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendExecute(id, "loop-1", "x <- 0; while (TRUE) x <- x + 1", json::object()));
    ASSERT_TRUE(collector.waitForMessage("execute_input", "loop-1").has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    ASSERT_TRUE(m_registry->sendInterrupt(id, "int-loop"));
    ASSERT_TRUE(collector.waitForMessage("interrupt_reply", "int-loop").has_value());

    auto executeReply = collector.waitForMessage("execute_reply", "loop-1");
    ASSERT_TRUE(executeReply.has_value()) << "the busy loop was not interrupted";
    EXPECT_NE(executeReply->at("content").at("status"), "ok");

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, InterruptWhileIdleDoesNothingAndDoesNotAbortTheNextExecution)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendInterrupt(id, "int-idle"));
    ASSERT_TRUE(collector.waitForMessage("interrupt_reply", "int-idle").has_value());

    // A stale break flag would abort this.
    ASSERT_TRUE(m_registry->sendExecute(id, "idle-then-run", "sum(1:10)", json::object()));
    auto reply = collector.waitForMessage("execute_reply", "idle-then-run");
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->at("content").at("status"), "ok") << reply->dump();

    m_registry->stopSession(id);
}

TEST_F(SessionRegistryTest, ShutdownRequestSentDuringAnExecutionIsHandledAfterItFinishes)
{
    // The control watcher only services interrupt_request while code runs;
    // anything else must be queued, not dropped or run mid-execution.
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(m_registry->sendExecute(id, "slow-1", "Sys.sleep(1); 'finished'", json::object()));
    ASSERT_TRUE(collector.waitForMessage("execute_input", "slow-1").has_value());

    // stopSession() sends shutdown_request while the sleep is still running.
    ASSERT_TRUE(m_registry->stopSession(id));

    auto executeReply = collector.find("execute_reply", "slow-1");
    ASSERT_TRUE(executeReply.has_value()) << "the running execution should have completed before shutdown";
    EXPECT_EQ(executeReply->at("content").at("status"), "ok");
}

TEST_F(SessionRegistryTest, SessionJsonReportsALiveHeartbeatEvenWhileTheKernelIsBusy)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;
    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    Collector collector(session);

    ASSERT_TRUE(waitFor([&]() { return sessionToJson(*session).at("heartbeat").value("hasPong", false); }, 20000))
        << "no heartbeat answer was ever recorded";

    json idle = sessionToJson(*session).at("heartbeat");
    EXPECT_GE(idle.at("rttMs").get<double>(), 0.0);
    EXPECT_LT(idle.at("rttMs").get<double>(), 1000.0);
    EXPECT_EQ(idle.at("misses").get<int>(), 0);

    // The kernel answers pings from its own thread, so a busy interpreter
    // (a 3 second sleep here) must not make the heartbeat go stale.
    ASSERT_TRUE(m_registry->sendExecute(id, "hb-busy", "Sys.sleep(3)", json::object()));
    ASSERT_TRUE(collector.waitForMessage("execute_input", "hb-busy").has_value());
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    json busy = sessionToJson(*session).at("heartbeat");
    EXPECT_LT(busy.at("sinceLastPongMs").get<long long>(), 1500) << busy.dump();
    EXPECT_EQ(busy.at("misses").get<int>(), 0) << busy.dump();
    EXPECT_FALSE(collector.find("execute_reply", "hb-busy").has_value()) << "the kernel should still have been busy";

    m_registry->stopSession(id);
}

// Own main() instead of linking GTest::gtest_main, as a second line of
// defense alongside the deliberate SessionRegistry leak in SetUp() above:
// std::_Exit() after RUN_ALL_TESTS() reports its result skips ordinary
// static/global destructors entirely (unlike a normal return from main()),
// so anything else that doesn't unblock on its own -- some other object's
// teardown, GTest's own internals -- can no longer hang the process either.
// CTest's own TIMEOUT (native/test/CMakeLists.txt) remains the backstop for
// a genuine in-test hang; this only covers the exit path once results are
// already in hand.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(result);
}
