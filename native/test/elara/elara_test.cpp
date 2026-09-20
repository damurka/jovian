// Coverage for elara.exe's own startup behavior, in isolation -- spawned
// directly rather than through SessionRegistry/KernelProcess (see the test
// below for why). Everything that drives elara through a real supervisor
// (create/execute/restart/stop) lives in native/test/themisto/
// session_registry_test.cpp instead, since that's exercising SessionRegistry's
// own logic, not elara's.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#ifndef _WIN32
#include <sys/wait.h>
#endif

TEST(ElaraTest, ExitsCleanlyWithAnActionableMessageWhenRCannotBeLoaded)
{
#ifndef ELARA_TEST_KERNEL_EXE
    GTEST_SKIP() << "ELARA_TEST_KERNEL_EXE not defined by CMake -- elara target not built alongside tests.";
#else
    if (!std::filesystem::exists(ELARA_TEST_KERNEL_EXE))
    {
        GTEST_SKIP() << "elara executable not found at " ELARA_TEST_KERNEL_EXE;
    }

    // Regression test for the dynamic R loading in native/src/elara/r/r_dynlib.cpp
    // (Windows: LoadLibrary(R.dll); Linux/macOS: dlopen(libR.so/.dylib)):
    // pointing --r-home/--r-path at a location with no real R installed
    // must make elara.exe exit quickly and cleanly -- exit code 1, an
    // actionable stderr message -- instead of hanging, crashing
    // ambiguously, or (before dynamic loading existed) the OS refusing to
    // start the process at all with no message from our own code at all.
    //
    // Deliberately bypasses SessionRegistry/KernelProcess: SessionRegistry::
    // createSessionWithId()'s waitForConfiguration() call has no timeout
    // (a separate, documented limitation -- see its comment in
    // session_registry.cpp) and would hang this test forever waiting for a
    // registration handshake that a kernel failing before R even loads will
    // never send. Spawning elara.exe directly via the same argv shape
    // KernelProcess::start() builds, and just waiting for it to exit on its
    // own, sidesteps that limitation entirely rather than tripping over it.
    std::string exePath = ELARA_TEST_KERNEL_EXE; // macro expands to a quoted string literal
    std::string bogusRHome = (std::filesystem::temp_directory_path() / "elara_test_no_such_r_here").string();
    std::filesystem::path outputFile = std::filesystem::temp_directory_path() / "elara_missing_r_test_output.txt";

    std::string command = "\"" + exePath + "\""
        " --r-home \"" + bogusRHome + "\""
        " --r-path \"" + bogusRHome + "\""
        " --registration-port 1 --key test-key --registration-ip 127.0.0.1"
        " > \"" + outputFile.string() + "\" 2>&1";

    // --r-path only *prepends* to setupEnvironment()'s PATH (engine.cpp) --
    // it never replaces it. std::system()'s child inherits this test
    // process's own PATH otherwise unchanged, so if the machine running
    // this test happens to have a real R bin directory on it already (this
    // machine does, from earlier R development work), LoadLibrary(R.dll)
    // still finds and loads a REAL R.dll via that inherited entry despite
    // the bogus --r-path -- confirmed directly: without this override, the
    // child got past loadRApi() entirely and instead failed ~8s later,
    // differently and non-deterministically, inside R's own init against
    // the bogus --r-home. Temporarily replacing (not just prepending to)
    // this test process's own PATH before spawning is what actually
    // guarantees R.dll can't be found, on any machine.
    const char* originalPathEnv = std::getenv("PATH");
    std::string originalPath = originalPathEnv ? originalPathEnv : "";
#ifdef _WIN32
    std::string minimalPath = "C:\\Windows\\System32;C:\\Windows";
    _putenv_s("PATH", minimalPath.c_str());
#else
    std::string minimalPath = "/usr/bin:/bin";
    setenv("PATH", minimalPath.c_str(), 1);
#endif

#ifdef _WIN32
    // std::system() runs this via `cmd /c <command>` -- when the command
    // starts with a quoted path followed by more quoted arguments, cmd's
    // own argument parsing strips only the outer quote pair it thinks
    // wraps "the program", mangling everything after it (confirmed: without
    // this, cmd reported "The filename, directory name, or volume label
    // syntax is incorrect." instead of ever running elara.exe at all).
    // Wrapping the whole command in one more pair of quotes is the standard
    // workaround.
    command = "\"" + command + "\"";
#endif

    int result = std::system(command.c_str());

#ifdef _WIN32
    _putenv_s("PATH", originalPath.c_str());
#else
    setenv("PATH", originalPath.c_str(), 1);
#endif

#ifdef _WIN32
    int exitCode = result;
#else
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
#endif

    std::ifstream in(outputFile);
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string output = buffer.str();
    in.close();
    std::filesystem::remove(outputFile);

    EXPECT_EQ(exitCode, 1);
    EXPECT_NE(output.find("[elara] FATAL"), std::string::npos) << "actual output was:\n" << output;
    EXPECT_NE(output.find("Is R installed"), std::string::npos) << "actual output was:\n" << output;
#endif
}
