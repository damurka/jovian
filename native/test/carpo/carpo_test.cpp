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
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

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

TEST(CarpoTest, CompleteRequestFindsRealAttributesOnAKnownObject)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    // Give the session a variable to complete against, matching how a real
    // notebook session would have state by the time a completion request
    // comes in.
    ASSERT_EQ(runCode(interpreter, "x = 'hello'").at("status").get<std::string>(), "ok");

    std::string code = "x.uppe";
    json reply = interpreter.completeRequest(code, static_cast<int>(code.size()));

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    auto matches = reply.at("matches").get<std::vector<std::string>>();
    EXPECT_NE(std::find(matches.begin(), matches.end(), "x.upper()"), matches.end());
}

TEST(CarpoTest, CompleteRequestReturnsNoMatchesForAnUnknownPrefix)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    std::string code = "totally_unknown_name_xyz";
    json reply = interpreter.completeRequest(code, static_cast<int>(code.size()));

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    EXPECT_TRUE(reply.at("matches").get<std::vector<std::string>>().empty());
}

TEST(CarpoTest, InspectRequestDescribesARealObject)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    ASSERT_EQ(runCode(interpreter, "x = 42").at("status").get<std::string>(), "ok");

    json reply = interpreter.inspectRequest("x", 1, 0);

    EXPECT_TRUE(reply.at("found").get<bool>());
    std::string text = reply.at("data").at("text/plain").get<std::string>();
    EXPECT_NE(text.find("int"), std::string::npos);
}

TEST(CarpoTest, InspectRequestReportsNotFoundForAnUndefinedName)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = interpreter.inspectRequest("totally_undefined_xyz", 20, 0);

    EXPECT_FALSE(reply.at("found").get<bool>());
}

TEST(CarpoTest, ExecuteRequestStreamsStdoutInRealTimeNotBatchedAtTheEnd)
{
    // Distinguishes real-time native-callback streaming from the earlier
    // io.StringIO-capture-then-publish-once design: a loop with several
    // separate print() calls must produce *multiple* separate publish()
    // invocations (one per write(), as each print() happens), not a single
    // publish() carrying all the output concatenated together at the end.
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    std::vector<std::string> streamedChunks;
    interpreter.registerPublisher([&](RequestContext, const std::string& msgType, json /*metadata*/, json content, buffer_sequence) {
        if (msgType == "stream" && content.value("name", "") == "stdout")
        {
            streamedChunks.push_back(content.value("text", ""));
        }
    });

    json reply = runCode(interpreter, "for i in range(3):\n    print(i)");

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    EXPECT_GT(streamedChunks.size(), 1u) << "expected multiple separate stream publishes, not one batched write";

    std::string combined;
    for (const auto& chunk : streamedChunks) combined += chunk;
    EXPECT_NE(combined.find("0"), std::string::npos);
    EXPECT_NE(combined.find("1"), std::string::npos);
    EXPECT_NE(combined.find("2"), std::string::npos);
}

#ifdef CARPO_TEST_PYTHON_HOME
TEST(CarpoTest, VenvPathActivatesTheVenvsSitePackagesDirectory)
{
    // Real end-to-end check, not just "the code compiles": actually
    // creates a throwaway venv with this test's own Python (`python -m
    // venv`), points CARPO_VENV_PATH at it (the same env var
    // carpo::Server::setupEnvironment() sets from EnvironmentConfig::
    // venv_path in production -- bridge/engine.cpp), and confirms the
    // bootstrap source's venv-activation snippet actually put that venv's
    // site-packages directory on sys.path.
    SKIP_IF_NO_PYTHON();

    std::filesystem::path venvDir = std::filesystem::temp_directory_path() / "carpo_test_venv";
    std::error_code ec;
    std::filesystem::remove_all(venvDir, ec);

#ifdef _WIN32
    std::string pythonExe = std::string(CARPO_TEST_PYTHON_HOME) + "/python.exe";
#else
    std::string pythonExe = std::string(CARPO_TEST_PYTHON_HOME) + "/bin/python3";
#endif
    std::string createVenvCmd = "\"" + pythonExe + "\" -m venv \"" + venvDir.string() + "\"";
#ifdef _WIN32
    // cmd.exe's own quoting gotcha: when a command STARTS with a quoted
    // path, it needs an extra outer pair of quotes around the whole thing
    // or cmd mis-parses it ("The filename, directory name, or volume label
    // syntax is incorrect.", confirmed directly) -- std::system() shells
    // out via cmd /c on this platform. Not needed on POSIX, where
    // std::system() uses /bin/sh directly.
    createVenvCmd = "\"" + createVenvCmd + "\"";
#endif
    int rc = std::system(createVenvCmd.c_str());
    ASSERT_EQ(rc, 0) << "failed to create throwaway venv via: " << createVenvCmd;

    std::string venvPathStr = venvDir.string();
#ifdef _WIN32
    _putenv_s("CARPO_VENV_PATH", venvPathStr.c_str());
#else
    setenv("CARPO_VENV_PATH", venvPathStr.c_str(), 1);
#endif

    {
        carpo::PyInterpreter interpreter(0, nullptr);

        json reply = runCode(interpreter,
            "import os, sys\n"
            "_venv = os.environ.get('CARPO_VENV_PATH', '')\n"
            "assert _venv, 'CARPO_VENV_PATH not set'\n"
            "assert any(p.startswith(_venv) for p in sys.path), sys.path\n");

        EXPECT_EQ(reply.at("status").get<std::string>(), "ok")
            << "evalue: " << reply.value("evalue", "");
    }

#ifdef _WIN32
    _putenv_s("CARPO_VENV_PATH", "");
#else
    unsetenv("CARPO_VENV_PATH");
#endif
    std::filesystem::remove_all(venvDir, ec);
}
#endif

TEST(CarpoTest, ExecuteRequestStreamsStderrSeparatelyFromStdout)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    std::vector<std::string> stdoutChunks;
    std::vector<std::string> stderrChunks;
    interpreter.registerPublisher([&](RequestContext, const std::string& msgType, json /*metadata*/, json content, buffer_sequence) {
        if (msgType != "stream") return;
        if (content.value("name", "") == "stdout") stdoutChunks.push_back(content.value("text", ""));
        if (content.value("name", "") == "stderr") stderrChunks.push_back(content.value("text", ""));
    });

    json reply = runCode(interpreter, "import sys\nprint('to stdout')\nprint('to stderr', file=sys.stderr)");

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    ASSERT_FALSE(stdoutChunks.empty());
    ASSERT_FALSE(stderrChunks.empty());

    std::string stdoutCombined, stderrCombined;
    for (const auto& c : stdoutChunks) stdoutCombined += c;
    for (const auto& c : stderrChunks) stderrCombined += c;
    EXPECT_NE(stdoutCombined.find("to stdout"), std::string::npos);
    EXPECT_NE(stderrCombined.find("to stderr"), std::string::npos);
}

namespace
{
    json runCodeWithExpressions(carpo::PyInterpreter& interpreter, const std::string& code, const json& userExpressions)
    {
        json reply;
        auto callback = [&](json r) { reply = std::move(r); };
        ExecuteRequestConfig config{ /*silent=*/false, /*store_history=*/false, /*allow_stdin=*/false };
        interpreter.executeRequest(RequestContext(), callback, code, config, userExpressions);
        return reply;
    }
}

TEST(CarpoTest, UserExpressionsAreEvaluatedAfterTheCodeAndEachReportsItsOwnResult)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = runCodeWithExpressions(interpreter, "x = 21",
        { { "double", "x * 2" }, { "boom", "undefined_name" } });

    ASSERT_EQ(reply.at("status").get<std::string>(), "ok");
    const json& results = reply.at("user_expressions");

    EXPECT_EQ(results.at("double").at("status").get<std::string>(), "ok");
    EXPECT_EQ(results.at("double").at("data").at("text/plain").get<std::string>(), "42");

    EXPECT_EQ(results.at("boom").at("status").get<std::string>(), "error");
    EXPECT_EQ(results.at("boom").at("ename").get<std::string>(), "NameError");
}

TEST(CarpoTest, UserExpressionsAreNotEvaluatedWhenTheCodeFails)
{
    SKIP_IF_NO_PYTHON();
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = runCodeWithExpressions(interpreter, "1 / 0", { { "one", "1" } });

    EXPECT_EQ(reply.at("status").get<std::string>(), "error");
    EXPECT_FALSE(reply.contains("user_expressions"));
}
