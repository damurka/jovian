// Coverage for KernelProcess (native/src/themisto/kernel_process.cpp) --
// spawn/isAlive/kill in isolation, using dummy_process_helper.exe instead of
// a real R-embedding kernel. Previously only exercised indirectly (and
// slowly -- R startup takes real time) through SessionRegistryTest.
#include <chrono>
#include <functional>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "themisto/kernel_process.hpp"

using namespace themisto;

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
        options.kernelExePath = THEMISTO_TEST_DUMMY_PROCESS_EXE;
        options.key = "run-until-killed";
        return options;
    }

    KernelProcessOptions quickExitOptions()
    {
        KernelProcessOptions options;
        options.kernelExePath = THEMISTO_TEST_DUMMY_PROCESS_EXE;
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

TEST(KernelProcessTest, PidIsAPositiveRealProcessIdWhileRunningAndZeroBeforeStart)
{
    KernelProcess process(longRunningOptions());
    EXPECT_EQ(process.pid(), 0) << "no process id should be reported before start()";

    process.start();
    EXPECT_GT(process.pid(), 0);

    process.kill();
}

TEST(KernelProcessTest, MemoryUsageBytesReturnsAValueForARunningProcess)
{
    // Best-effort (nullopt on platforms/situations where it can't be
    // determined -- see KernelProcess::memoryUsageBytes()'s file comment),
    // but a real, just-started process on a platform this is implemented
    // for should always report *something* greater than zero.
    KernelProcess process(longRunningOptions());
    process.start();
    ASSERT_TRUE(process.isAlive());

    auto memory = process.memoryUsageBytes();

#if defined(_WIN32) || defined(__linux__)
    ASSERT_TRUE(memory.has_value());
    EXPECT_GT(memory.value(), 0u);
#else
    // macOS: deliberately not implemented (no way to verify Mach/libproc
    // APIs without a real Mac) -- nullopt is the documented, honest answer.
    EXPECT_FALSE(memory.has_value());
#endif

    process.kill();
}

TEST(KernelProcessTest, MemoryUsageBytesIsNulloptAfterTheProcessIsKilled)
{
    KernelProcess process(longRunningOptions());
    process.start();
    ASSERT_TRUE(process.isAlive());
    process.kill();

    EXPECT_FALSE(process.memoryUsageBytes().has_value());
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

TEST(KernelProcessTest, StartThrowsWhenTheExecutableDoesNotExist)
{
    // Covers KernelProcess::start()'s CreateProcessA failure branch
    // (native/src/themisto/kernel_process.cpp) -- previously untested,
    // every other test here spawns a real (dummy) executable successfully.
    KernelProcessOptions options;
    options.kernelExePath = "C:\\this\\path\\does\\not\\exist\\elara.exe";

    KernelProcess process(options);
    EXPECT_THROW(process.start(), std::runtime_error);
}

TEST(KernelProcessTest, DescribeStatusReportsStillRunningWhileAlive)
{
    KernelProcess process(longRunningOptions());
    process.start();
    ASSERT_TRUE(process.isAlive());

    EXPECT_NE(process.describeStatus().find("still running"), std::string::npos);

    process.kill();
}

TEST(KernelProcessTest, DescribeStatusReportsTheExitCodeAfterANaturalExit)
{
    KernelProcess process(quickExitOptions());
    process.start();
    ASSERT_TRUE(waitFor([&]() { return !process.isAlive(); }, 5000));

    // Same underlying exit code (0) on both platforms, but describeStatus()
    // formats it differently: hex on Windows (its case labels below are
    // NTSTATUS codes, conventionally written in hex, e.g. 0xC0000005), plain
    // decimal on POSIX (the WEXITSTATUS() convention -- `echo $?` et al.).
#ifdef _WIN32
    EXPECT_NE(process.describeStatus().find("exited with code 0x0"), std::string::npos);
#else
    EXPECT_NE(process.describeStatus().find("exited with code 0"), std::string::npos);
#endif
}

TEST(KernelProcessTest, DescribeStatusBeforeStartingReportsNeverStarted)
{
    KernelProcessOptions options;
    options.kernelExePath = THEMISTO_TEST_DUMMY_PROCESS_EXE;

    KernelProcess process(options);

    EXPECT_NE(process.describeStatus().find("never started"), std::string::npos);
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
