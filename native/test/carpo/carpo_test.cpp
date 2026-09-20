// Coverage for carpo's scaffolding (native/src/carpo/interpreter_py.cpp) --
// NOT a working Python kernel, see that file's comment. This just confirms
// the scaffold behaves exactly as documented: kernelInfoRequest() correctly
// identifies the kernel over the Jupyter protocol, and every other request
// that would need real Python execution comes back as a clear, structured
// "not implemented" error rather than crashing, hanging, or silently
// pretending to succeed.
//
// Constructs one PyInterpreter directly, in-process, rather than spawning a
// real carpo.exe (unlike native/test/elara/elara_test.cpp) -- carpo.exe's
// registration-handshake launch mode needs a listening supervisor endpoint,
// more setup than this scaffold-behavior check needs. Only one PyInterpreter
// is ever constructed in this binary's lifetime (a single TEST case): its
// constructor calls adrastea::registerInterpreter(), a process-wide
// singleton that only allows one registration to ever succeed.
#include <string>

#include <gtest/gtest.h>

#include "carpo/interpreter_py.hpp"
#include "adrastea/interpreter.hpp"

using namespace adrastea;

TEST(CarpoTest, KernelInfoRequestIdentifiesItselfAsAScaffoldedPythonKernel)
{
    carpo::PyInterpreter interpreter(0, nullptr);

    json info = interpreter.kernelInfoRequest();

    EXPECT_EQ(info.at("implementation").get<std::string>(), "carpo");
    EXPECT_EQ(info.at("language_info").at("name").get<std::string>(), "python");
    EXPECT_EQ(info.at("language_info").at("file_extension").get<std::string>(), ".py");
}

TEST(CarpoTest, ExecuteRequestRepliesWithANotImplementedErrorInsteadOfHangingOrCrashing)
{
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply;
    bool replied = false;
    auto callback = [&](json r) {
        reply = std::move(r);
        replied = true;
    };

    ExecuteRequestConfig config{ /*silent=*/true, /*store_history=*/false, /*allow_stdin=*/false };
    interpreter.executeRequest(RequestContext(), callback, "print('hello')", config, json::object());

    ASSERT_TRUE(replied);
    EXPECT_EQ(reply.at("status").get<std::string>(), "error");
    EXPECT_EQ(reply.at("ename").get<std::string>(), "NotImplementedError");
    EXPECT_NE(reply.at("evalue").get<std::string>().find("scaffolding"), std::string::npos);
}

TEST(CarpoTest, ShutdownRequestSucceedsSinceThereIsNoRealInterpreterStateToTearDown)
{
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = interpreter.shutdownRequest(/*restart=*/false);

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
}

TEST(CarpoTest, IsCompleteRequestHonestlyReportsUnknownRatherThanGuessing)
{
    carpo::PyInterpreter interpreter(0, nullptr);

    json reply = interpreter.isCompleteRequest("def f():");

    EXPECT_EQ(reply.at("status").get<std::string>(), "unknown");
}
