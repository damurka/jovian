// Coverage for carpo's real (execute + is_complete) Python execution --
// see native/include/carpo/interpreter_py.hpp's file comment for what's
// real vs. still stubbed in this pass.
//
// Constructs one PyInterpreter directly, in-process, rather than spawning a
// real carpo.exe (unlike native/test/elara/elara_test.cpp) -- carpo.exe's
// registration-handshake launch mode needs a listening supervisor endpoint,
// more setup than this needs. Each TEST constructs and destroys its own
// PyInterpreter; PyInterpreter's ctor/dtor are specifically written to make
// that safe (see interpreter_py.cpp's finalizeIfOwned()) by tracking which
// instance actually owns the process-wide Py_Initialize()/Py_FinalizeEx()
// lifecycle -- CPython explicitly supports a full finalize-then-reinitialize
// cycle within the same process, which is all this sequential (never
// concurrent) construct-then-destruct pattern relies on.
#include <cstdlib>
#include <string>

#include <gtest/gtest.h>

#include "carpo/interpreter_py.hpp"
#include "adrastea/interpreter.hpp"

using namespace adrastea;

namespace
{
    // Mirrors elara_test.cpp's own CARPO_TEST_PYTHON_HOME/GTEST_SKIP
    // pattern for ELARA_TEST_R_HOME: CMake only defines this when a Python
    // interpreter was actually found on the machine building/running these
    // tests (native/test/CMakeLists.txt). Sets PYTHONHOME (mirroring how
    // carpo::Server::setupEnvironment() sets it in production, from
    // EnvironmentConfig::python_home) before constructing a PyInterpreter,
    // which reads it via std::getenv() in its constructor.
    bool setUpPythonHomeOrSkip()
    {
#ifdef CARPO_TEST_PYTHON_HOME
#ifdef _WIN32
        _putenv_s("PYTHONHOME", CARPO_TEST_PYTHON_HOME);
#else
        setenv("PYTHONHOME", CARPO_TEST_PYTHON_HOME, 1);
#endif
        return true;
#else
        return false;
#endif
    }
}

#define SKIP_IF_NO_PYTHON() \
    if (!setUpPythonHomeOrSkip()) { GTEST_SKIP() << "No Python interpreter found at configure time (CARPO_TEST_PYTHON_HOME not set)"; }

TEST(CarpoTest, KernelInfoRequestIdentifiesItselfAsARealPythonKernel)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json info = interpreter.kernelInfoRequest();

    EXPECT_EQ(info.at("implementation").get<std::string>(), "carpo");
    EXPECT_EQ(info.at("language_info").at("name").get<std::string>(), "python");
    EXPECT_EQ(info.at("language_info").at("file_extension").get<std::string>(), ".py");
    // Real version, not the scaffold's "unknown (not yet embedded)" --
    // sanity-checked as "starts with a digit" rather than an exact string
    // so this doesn't need updating every time the test machine's Python
    // version changes.
    std::string version = info.at("language_info").at("version").get<std::string>();
    ASSERT_FALSE(version.empty());
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(version[0])));
}

namespace
{
    json runCode(carpo::PyInterpreter& interpreter, const std::string& code)
    {
        json reply;
        bool replied = false;
        auto callback = [&](json r) {
            reply = std::move(r);
            replied = true;
        };

        ExecuteRequestConfig config{ /*silent=*/false, /*store_history=*/false, /*allow_stdin=*/false };
        interpreter.executeRequest(RequestContext(), callback, code, config, json::object());

        EXPECT_TRUE(replied) << "executeRequestImpl must always reply exactly once, synchronously";
        return reply;
    }
}

TEST(CarpoTest, ExecuteRequestActuallyRunsPythonCodeAndPersistsVariablesAcrossCalls)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json first = runCode(interpreter, "x = 21 * 2");
    EXPECT_EQ(first.at("status").get<std::string>(), "ok");

    // No separate execute_result publish channel is visible from this
    // reply alone (publishExecutionResult goes through registerPublisher(),
    // not wired up in this test) -- so the persistence check reads x back
    // via a trailing expression instead, which the reply DOES carry no
    // matter how it's published, since the check that matters here is
    // "did the assignment actually happen and stick in the shared
    // __main__ namespace", not "was it displayed".
    json second = runCode(interpreter, "x");
    EXPECT_EQ(second.at("status").get<std::string>(), "ok");
}

TEST(CarpoTest, ExecuteRequestReportsARealPythonExceptionAsAStructuredError)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = runCode(interpreter, "1 / 0");

    EXPECT_EQ(reply.at("status").get<std::string>(), "error");
    EXPECT_EQ(reply.at("ename").get<std::string>(), "ZeroDivisionError");
}

TEST(CarpoTest, ExecuteRequestSurvivesAcrossMultipleSequentialInterpreterInstances)
{
    // Regression check for the finalize/reinitialize lifecycle described in
    // this file's header comment: three PyInterpreters, constructed and
    // destroyed one after another (not concurrently), each in a fresh
    // Py_Initialize()/Py_FinalizeEx() cycle.
    SKIP_IF_NO_PYTHON();
    for (int i = 0; i < 3; ++i)
    {
        carpo::PyInterpreter interpreter(0, nullptr);
        json reply = runCode(interpreter, "1 + 1");
        EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    }
}

TEST(CarpoTest, ShutdownRequestSucceedsAndFinalizesTheRealInterpreter)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = interpreter.shutdownRequest(/*restart=*/false);

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
}

TEST(CarpoTest, IsCompleteRequestRecognizesCompleteCode)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    EXPECT_EQ(interpreter.isCompleteRequest("1 + 1").at("status").get<std::string>(), "complete");
}

TEST(CarpoTest, IsCompleteRequestRecognizesIncompleteCode)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    EXPECT_EQ(interpreter.isCompleteRequest("def f():").at("status").get<std::string>(), "incomplete");
}

TEST(CarpoTest, IsCompleteRequestRecognizesInvalidCode)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    EXPECT_EQ(interpreter.isCompleteRequest("def f(:").at("status").get<std::string>(), "invalid");
}
