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
    std::string resolveRHome()
    {
        if (const char* fromEnv = std::getenv("R_HOME"))
        {
            return fromEnv;
        }
#ifdef _WIN32
        return "C:/Program Files/R/R-4.6.0";
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
            m_registry = std::make_unique<SessionRegistry>(DATASUITE_TEST_KERNEL_EXE, "127.0.0.1");
            m_registry->startRegistrationListener();
#endif
        }

        std::string m_rHome;
        std::unique_ptr<SessionRegistry> m_registry;
    };

    // 90s: generous on purpose. A real R startup (Rf_initEmbeddedR + loading
    // the bundled 'hera' package) plus the registration handshake can take
    // several seconds on a cold filesystem cache; this only needs to bound
    // the case where something is actually broken (e.g. the known
    // waitForConfiguration() no-timeout limitation noted in
    // session_registry.cpp), not the happy path's normal latency.
    constexpr int kTimeoutMs = 90000;
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

// Own main() instead of linking GTest::gtest_main: process exit after a
// clean stopSession() intermittently hung in testing here, with the kernel
// process already gone and every C++ call (stopSession() itself included)
// already returned per manual tracing -- something in ordinary static/
// global teardown for the ZMQ-based client stack was not reliably
// unblocking. This is the same class of issue already documented and
// accepted elsewhere in this codebase (see test/integration/engine.test.ts's
// comment on native addon background threads outliving node --test's own
// teardown, and scripts/test.js's corresponding hard-kill-after-timeout
// workaround): std::_Exit() after RUN_ALL_TESTS() reports its result skips
// normal static/global destructors entirely, so whatever isn't unblocking
// on its own can no longer hang the process -- CTest's own TIMEOUT
// (native/test/CMakeLists.txt) remains the backstop for a genuine in-test
// hang, this only covers the exit path once results are already in hand.
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(result);
}
