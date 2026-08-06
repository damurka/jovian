// Exercises SessionRegistry (native/src/supervisor/session_registry.cpp)
// directly, bypassing HttpApi/WsRelay -- this is the piece the
// implementation plan called for testing "against a fake ClientZmq/kernel
// where feasible" and that never got covered. A fake ClientZmq would need
// SessionRegistry to accept an injectable client/process factory, which it
// doesn't (and refactoring it to support that is a bigger, riskier change
// than this test warrants); using the real datasuite-r executable this test
// binary is built alongside is a smaller, higher-fidelity alternative that
// still isolates SessionRegistry from HTTP/WS (already covered by
// test/integration/session-manager.test.ts on the TypeScript side).
//
// Needs a working R installation to actually start a kernel -- skips itself
// (GTEST_SKIP) rather than failing when one isn't found, matching the
// skip-if-native-binary-missing pattern already used by the TS test suite.
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "datasuite/json.hpp"
#include "supervisor/session_registry.hpp"

using namespace datasuite;
using namespace datasuite::supervisor;

namespace
{
    // R_HOME env var first (lets a developer/CI override), then the R
    // installation CMake itself already found and validated at configure
    // time (find_package(R REQUIRED) in the root CMakeLists.txt, propagated
    // here as DATASUITE_TEST_R_HOME) -- not a path guessed at from one
    // machine. Neither present means no usable fallback; SetUp() skips the
    // test rather than pointing R_HOME at somewhere that doesn't exist.
    std::string resolveRHome()
    {
        if (const char* fromEnv = std::getenv("R_HOME"))
        {
            return fromEnv;
        }
#ifdef DATASUITE_TEST_R_HOME
        return DATASUITE_TEST_R_HOME;
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
#ifndef DATASUITE_TEST_KERNEL_EXE
            GTEST_SKIP() << "DATASUITE_TEST_KERNEL_EXE not defined by CMake -- datasuite-r target not built alongside tests.";
#else
            if (!std::filesystem::exists(DATASUITE_TEST_KERNEL_EXE))
            {
                GTEST_SKIP() << "datasuite-r executable not found at " DATASUITE_TEST_KERNEL_EXE;
            }
            m_rHome = rHome;
            // Deliberately leaked (not a unique_ptr): ~SessionRegistry()'s
            // implicit member teardown -- specifically something downstream
            // of the registration listener's ZMQ context/socket -- was
            // confirmed (via native/test/plain_diag.cpp, not part of this
            // suite) to hang on process exit even though every explicit
            // SessionRegistry/Session call involved returns normally.
            // Production code never hits this either way: datasuite-
            // supervisor's own process is always force-killed by its parent
            // (SupervisorClient.kill() in lib/session/supervisor-client.ts),
            // never gracefully destructed. Leaking here and relying on
            // std::_Exit() (this file's main(), below) to reclaim the
            // process's resources sidesteps a destructor that's confirmed
            // broken rather than papering over it with a guess.
            m_registry = new SessionRegistry(DATASUITE_TEST_KERNEL_EXE, "127.0.0.1");
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
    auto* registry = new SessionRegistry("unused-kernel-exe-path", "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_EQ(registry->getSession("does-not-exist"), nullptr);
}

TEST(SessionRegistryEmptyStateTest, ListSessionsOnEmptyRegistryReturnsEmptyArray)
{
    auto* registry = new SessionRegistry("unused-kernel-exe-path", "127.0.0.1");
    registry->startRegistrationListener();

    json sessions = registry->listSessions();
    EXPECT_TRUE(sessions.is_array());
    EXPECT_TRUE(sessions.empty());
}

TEST(SessionRegistryEmptyStateTest, StopSessionOnUnknownIdReturnsFalse)
{
    auto* registry = new SessionRegistry("unused-kernel-exe-path", "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_FALSE(registry->stopSession("does-not-exist"));
}

TEST(SessionRegistryEmptyStateTest, SendExecuteOnUnknownIdReturnsFalse)
{
    auto* registry = new SessionRegistry("unused-kernel-exe-path", "127.0.0.1");
    registry->startRegistrationListener();

    EXPECT_FALSE(registry->sendExecute("does-not-exist", "msg-1", "1 + 1", json::object()));
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

TEST_F(SessionRegistryTest, StopSessionMarksItStoppedAndTerminatesTheProcess)
{
    SessionOptions options;
    options.rHome = m_rHome;

    std::string error;
    std::string id = m_registry->createSession(options, error);
    ASSERT_FALSE(id.empty()) << "createSession failed: " << error;

    EXPECT_TRUE(m_registry->stopSession(id));

    auto session = m_registry->getSession(id);
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(session->status.load(), SessionStatus::Stopped);
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
