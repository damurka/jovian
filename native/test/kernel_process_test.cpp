// Coverage for KernelProcess (native/src/supervisor/kernel_process.cpp) --
// spawn/isAlive/kill in isolation, using dummy_process_helper.exe instead of
// a real R-embedding kernel. Previously only exercised indirectly (and
// slowly -- R startup takes real time) through SessionRegistryTest.
#include <chrono>
#include <functional>
#include <thread>

#include <gtest/gtest.h>

#include "supervisor/kernel_process.hpp"

using namespace datasuite::supervisor;

namespace
{
    // Polls `predicate` until it returns true or `timeoutMs` elapses.
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

    KernelProcessOptions longRunningOptions()
    {
        KernelProcessOptions options;
        options.kernelExePath = DATASUITE_TEST_DUMMY_PROCESS_EXE;
        options.key = "run-until-killed";
        return options;
    }

    KernelProcessOptions quickExitOptions()
    {
        KernelProcessOptions options;
        options.kernelExePath = DATASUITE_TEST_DUMMY_PROCESS_EXE;
        options.key = "quick-exit";
        return options;
    }
}

TEST(KernelProcessTest, IsAliveIsTrueWhileTheProcessIsRunning)
{
    KernelProcess process(longRunningOptions());
    process.start();

    EXPECT_TRUE(process.isAlive());

    process.kill();
}

TEST(KernelProcessTest, KillTerminatesARunningProcess)
{
    KernelProcess process(longRunningOptions());
    process.start();
    ASSERT_TRUE(process.isAlive());

    process.kill();

    EXPECT_FALSE(process.isAlive());
}

TEST(KernelProcessTest, IsAliveBecomesFalseAfterTheProcessExitsOnItsOwn)
{
    KernelProcess process(quickExitOptions());
    process.start();

    bool exited = waitFor([&]() { return !process.isAlive(); }, 5000);
    EXPECT_TRUE(exited);
}

TEST(KernelProcessTest, KillIsSafeToCallOnAnAlreadyExitedProcess)
{
    // Guards against the exact class of bug fixed elsewhere in this session
    // (~SessionRegistry() calling stopChannels() twice) -- kill() should be
    // idempotent-safe, not just safe to call once.
    KernelProcess process(quickExitOptions());
    process.start();
    ASSERT_TRUE(waitFor([&]() { return !process.isAlive(); }, 5000));

    process.kill();
    process.kill();

    SUCCEED();
}

TEST(KernelProcessTest, DestructorKillsAStillRunningProcess)
{
    {
        KernelProcess process(longRunningOptions());
        process.start();
        ASSERT_TRUE(process.isAlive());
    }
    // No direct assertion possible on a destroyed object -- this test's
    // value is running under a leak/handle checker (or just not hanging
    // the test process on exit), matching what ~KernelProcess() actually
    // promises (kill() in its body).
    SUCCEED();
}
