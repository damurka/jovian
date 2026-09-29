// The Stata kernel against a real Stata (17 or newer, licensed). Set
// STATA_HOME to the Stata directory to run these; without it, or when Stata
// cannot start (no license), every test is skipped with the reason.
//
// Stata can only be started once per process, so all tests share one
// StataInterpreter, created on first use -- unlike carpo_test.cpp, where each
// test makes its own PyInterpreter.
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "adrastea/interpreter.hpp"
#include "callisto/interpreter_stata.hpp"

using namespace adrastea;

namespace
{
    struct Published
    {
        std::string msgType;
        json content;
    };

    std::vector<Published>& published()
    {
        static std::vector<Published> messages;
        return messages;
    }

    std::string& startError()
    {
        static std::string error;
        return error;
    }

    callisto::StataInterpreter* stata()
    {
        static std::unique_ptr<callisto::StataInterpreter> instance;
        static bool tried = false;
        if (!tried)
        {
            tried = true;
            if (!std::getenv("STATA_HOME"))
            {
                startError() = "STATA_HOME is not set";
                return nullptr;
            }
            try
            {
                instance = std::make_unique<callisto::StataInterpreter>();
                instance->registerPublisher([](RequestContext, const std::string& msgType, json, json content, buffer_sequence) {
                    published().push_back({ msgType, std::move(content) });
                });
            }
            catch (const std::exception& e)
            {
                startError() = e.what();
            }
        }
        return instance.get();
    }

    json run(const std::string& code, bool silent = false, json userExpressions = json::object())
    {
        json reply;
        bool replied = false;
        ExecuteRequestConfig config{ silent, /*store_history=*/false, /*allow_stdin=*/false };
        stata()->executeRequest(RequestContext(), [&](json r) {
            reply = std::move(r);
            replied = true;
        }, code, config, std::move(userExpressions));
        EXPECT_TRUE(replied) << "executeRequestImpl must reply exactly once";
        return reply;
    }

    // Everything published as stdout since the last clearPublished().
    std::string stdoutText()
    {
        stata()->flushStreams();
        std::string text;
        for (const auto& message : published())
        {
            if (message.msgType == "stream" && message.content.value("name", "") == "stdout")
            {
                text += message.content.value("text", "");
            }
        }
        return text;
    }

    std::vector<Published> ofType(const std::string& msgType)
    {
        std::vector<Published> matches;
        for (const auto& message : published())
        {
            if (message.msgType == msgType) matches.push_back(message);
        }
        return matches;
    }

    void clearPublished()
    {
        if (stata()) stata()->flushStreams();
        published().clear();
    }
}

#define SKIP_WITHOUT_STATA() \
    if (!stata()) { GTEST_SKIP() << "No Stata to run: " << startError(); } \
    clearPublished()

TEST(CallistoTest, KernelInfoReportsStata)
{
    SKIP_WITHOUT_STATA();
    json info = stata()->kernelInfoRequest();

    EXPECT_EQ(info.at("implementation").get<std::string>(), "callisto");
    EXPECT_EQ(info.at("language_info").at("name").get<std::string>(), "stata");
    EXPECT_EQ(info.at("language_info").at("file_extension").get<std::string>(), ".do");
    std::string version = info.at("language_info").at("version").get<std::string>();
    ASSERT_FALSE(version.empty());
    EXPECT_GE(std::atof(version.c_str()), 17.0);
}

TEST(CallistoTest, ExecuteStreamsStataOutput)
{
    SKIP_WITHOUT_STATA();
    json reply = run("display 6 * 7");

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    EXPECT_NE(stdoutText().find("42"), std::string::npos) << stdoutText();
}

TEST(CallistoTest, DataAndLocalMacrosPersistAcrossCells)
{
    SKIP_WITHOUT_STATA();
    ASSERT_EQ(run("sysuse auto, clear\nlocal answer = 42").at("status").get<std::string>(), "ok");
    clearPublished();

    json reply = run("display `answer' + _N");

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    // auto.dta has 74 observations.
    EXPECT_NE(stdoutText().find("116"), std::string::npos) << stdoutText();
}

TEST(CallistoTest, AFailingCommandIsAnErrorWithStatasReturnCode)
{
    SKIP_WITHOUT_STATA();
    run("sysuse auto, clear");
    clearPublished();

    json reply = run("regress price nosuchvar");

    EXPECT_EQ(reply.at("status").get<std::string>(), "error");
    EXPECT_EQ(reply.at("ename").get<std::string>(), "r(111)");
    EXPECT_NE(reply.at("evalue").get<std::string>().find("nosuchvar"), std::string::npos)
        << reply.at("evalue").get<std::string>();
    EXPECT_EQ(ofType("error").size(), 1u);
}

TEST(CallistoTest, MultiLineConstructsRun)
{
    SKIP_WITHOUT_STATA();
    json reply = run(
        "/* a comment\n   over two lines */\n"
        "forvalues i = 1/3 {\n"
        "    display \"row `i'\"\n"
        "}\n"
        "display 1 + ///\n"
        "    2");

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    std::string out = stdoutText();
    EXPECT_NE(out.find("row 3"), std::string::npos) << out;
}

TEST(CallistoTest, AGraphIsPublishedAsAPng)
{
    SKIP_WITHOUT_STATA();
    run("sysuse auto, clear");
    clearPublished();

    json reply = run("scatter price mpg");

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    auto displays = ofType("display_data");
    ASSERT_EQ(displays.size(), 1u);
    std::string png = displays[0].content.at("data").at("image/png").get<std::string>();
    // base64 of the PNG signature.
    EXPECT_EQ(png.rfind("iVBORw0KGgo", 0), 0u);
}

TEST(CallistoTest, ASilentExecutionPublishesNothing)
{
    SKIP_WITHOUT_STATA();
    json reply = run("display 12345", /*silent=*/true);

    EXPECT_EQ(reply.at("status").get<std::string>(), "ok");
    EXPECT_EQ(stdoutText().find("12345"), std::string::npos);
}

TEST(CallistoTest, UserExpressionsAreDisplayed)
{
    SKIP_WITHOUT_STATA();
    json reply = run("scalar s = 5", false, json{ { "twice", "2 * s" }, { "bad", "nosuchthing" } });

    ASSERT_EQ(reply.at("status").get<std::string>(), "ok");
    const json& results = reply.at("user_expressions");
    EXPECT_EQ(results.at("twice").at("status").get<std::string>(), "ok");
    EXPECT_EQ(results.at("twice").at("data").at("text/plain").get<std::string>(), "10");
    EXPECT_EQ(results.at("bad").at("status").get<std::string>(), "error");
}

TEST(CallistoTest, RResultsSurviveTheGraphAndInspectMachinery)
{
    SKIP_WITHOUT_STATA();
    run("sysuse auto, clear\nquietly summarize price");
    stata()->inspectRequest("mpg", 1, 0);
    clearPublished();

    run("display r(N)");

    EXPECT_NE(stdoutText().find("74"), std::string::npos) << stdoutText();
}

TEST(CallistoTest, CompleteFindsVariablesAndMacros)
{
    SKIP_WITHOUT_STATA();
    run("sysuse auto, clear\nlocal mylocal 1\nglobal myglobal 2");

    json variables = stata()->completeRequest("summarize pr", 12);
    auto names = variables.at("matches").get<std::vector<std::string>>();
    EXPECT_NE(std::find(names.begin(), names.end(), "price"), names.end());
    EXPECT_EQ(variables.at("cursor_start").get<int>(), 10);

    auto locals = stata()->completeRequest("display `myl", 12).at("matches").get<std::vector<std::string>>();
    EXPECT_NE(std::find(locals.begin(), locals.end(), "mylocal"), locals.end());

    auto globals = stata()->completeRequest("display $myg", 12).at("matches").get<std::vector<std::string>>();
    EXPECT_NE(std::find(globals.begin(), globals.end(), "myglobal"), globals.end());
}

TEST(CallistoTest, InspectDescribesAVariable)
{
    SKIP_WITHOUT_STATA();
    run("sysuse auto, clear");

    json reply = stata()->inspectRequest("summarize price", 12, 0);

    ASSERT_TRUE(reply.at("found").get<bool>());
    std::string text = reply.at("data").at("text/plain").get<std::string>();
    EXPECT_NE(text.find("price"), std::string::npos) << text;
    EXPECT_FALSE(stata()->inspectRequest("nosuchvar", 3, 0).at("found").get<bool>());
}

TEST(CallistoTest, IsCompleteUsesStataSyntax)
{
    SKIP_WITHOUT_STATA();
    EXPECT_EQ(stata()->isCompleteRequest("forvalues i = 1/3 {").at("status").get<std::string>(), "incomplete");
    EXPECT_EQ(stata()->isCompleteRequest("display 1").at("status").get<std::string>(), "complete");
}
