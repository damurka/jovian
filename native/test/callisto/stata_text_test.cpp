// The text processing behind the Stata kernel (callisto/stata/stata_text.hpp):
// is_complete, what a completion request is completing, and finding the
// error message in Stata's output. No Stata needed.
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "callisto/stata/stata_text.hpp"

using namespace callisto::text;

TEST(StataTextIsComplete, OrdinaryCommandsAreComplete)
{
    EXPECT_EQ(isComplete("sysuse auto, clear"), "complete");
    EXPECT_EQ(isComplete("sysuse auto\nsummarize price\n"), "complete");
    EXPECT_EQ(isComplete(""), "complete");
}

TEST(StataTextIsComplete, AnOpenBlockIsIncompleteUntilItCloses)
{
    EXPECT_EQ(isComplete("forvalues i = 1/3 {"), "incomplete");
    EXPECT_EQ(isComplete("forvalues i = 1/3 {\n  display `i'"), "incomplete");
    EXPECT_EQ(isComplete("forvalues i = 1/3 {\n  display `i'\n}"), "complete");
    EXPECT_EQ(isComplete("program define f\n  if 1 {\n    display 1\n  }\nend"), "complete");
}

TEST(StataTextIsComplete, AStrayClosingBraceIsInvalid)
{
    EXPECT_EQ(isComplete("}"), "invalid");
    EXPECT_EQ(isComplete("display 1\n}\n{"), "invalid");
}

TEST(StataTextIsComplete, BracesInStringsAndCommentsDoNotCount)
{
    EXPECT_EQ(isComplete("display \"{\""), "complete");
    EXPECT_EQ(isComplete("display `\"say \"{\" twice\"'"), "complete");
    EXPECT_EQ(isComplete("display 1 // {"), "complete");
    EXPECT_EQ(isComplete("* {"), "complete");
    EXPECT_EQ(isComplete("display 1 /* { */"), "complete");
}

TEST(StataTextIsComplete, AnOpenBlockCommentIsIncomplete)
{
    EXPECT_EQ(isComplete("/* a comment"), "incomplete");
    EXPECT_EQ(isComplete("/* a comment\nstill */ display 1"), "complete");
    // Stata's block comments nest.
    EXPECT_EQ(isComplete("/* outer /* inner */ still outer"), "incomplete");
}

TEST(StataTextIsComplete, ALineContinuationIsIncomplete)
{
    EXPECT_EQ(isComplete("regress price ///"), "incomplete");
    EXPECT_EQ(isComplete("regress price ///\n"), "incomplete");
    EXPECT_EQ(isComplete("regress price ///\n  mpg weight"), "complete");
}

TEST(StataTextIsComplete, SlashesInsideAWordAreNotAComment)
{
    // `//` only starts a comment at the start of a line or after a blank.
    EXPECT_EQ(isComplete("use http://example.com/data {"), "incomplete");
}

TEST(StataTextCompletion, ABareNameCompletesVariables)
{
    CompletionToken token = completionToken("summarize pr", 12);
    EXPECT_EQ(token.kind, CompletionKind::Variable);
    EXPECT_EQ(token.prefix, "pr");
    EXPECT_EQ(token.start, 10);
}

TEST(StataTextCompletion, ABacktickCompletesLocals)
{
    CompletionToken token = completionToken("display `my", 11);
    EXPECT_EQ(token.kind, CompletionKind::Local);
    EXPECT_EQ(token.prefix, "my");
    EXPECT_EQ(token.start, 9);

    EXPECT_EQ(completionToken("display `", 9).kind, CompletionKind::Local);
}

TEST(StataTextCompletion, ADollarCompletesGlobals)
{
    CompletionToken plain = completionToken("display $S_", 11);
    EXPECT_EQ(plain.kind, CompletionKind::Global);
    EXPECT_EQ(plain.prefix, "S_");

    CompletionToken braced = completionToken("display ${S_", 12);
    EXPECT_EQ(braced.kind, CompletionKind::Global);
    EXPECT_EQ(braced.start, 10);
}

TEST(StataTextCompletion, NothingToCompleteAfterASpaceOrANumber)
{
    EXPECT_EQ(completionToken("summarize ", 10).kind, CompletionKind::None);
    EXPECT_EQ(completionToken("display 12", 10).kind, CompletionKind::None);
}

TEST(StataTextCompletion, ACursorPastTheEndIsClamped)
{
    CompletionToken token = completionToken("list mp", 99);
    EXPECT_EQ(token.prefix, "mp");
}

TEST(StataTextCompletion, MatchingNamesKeepsOrderAndDropsDuplicates)
{
    std::vector<std::string> names = { "price", "mpg", "prod", "price" };
    EXPECT_EQ(matchingNames(names, "pr"), (std::vector<std::string>{ "price", "prod" }));
    EXPECT_EQ(matchingNames(names, ""), (std::vector<std::string>{ "price", "mpg", "prod" }));
}

TEST(StataTextCompletion, SplitNamesSplitsOnAnyWhitespace)
{
    EXPECT_EQ(splitNames("make price  mpg\nrep78\n"), (std::vector<std::string>{ "make", "price", "mpg", "rep78" }));
    EXPECT_TRUE(splitNames("  \n").empty());
}

TEST(StataTextTokenAt, FindsTheWholeNameAroundTheCursor)
{
    EXPECT_EQ(tokenAt("summarize price mpg", 12), "price");
    EXPECT_EQ(tokenAt("summarize price", 15), "price");
    EXPECT_EQ(tokenAt("summarize ", 10), "");
}

TEST(StataTextErrorMessage, FindsTheMessageAboveTheReturnCode)
{
    std::string output =
        ". sysuse auto\n(1978 automobile data)\n\n. regress price nosuchvar\n"
        "variable nosuchvar not found\nr(111);\n\nend of do-file\n\nr(111);\n";
    EXPECT_EQ(errorMessage(output, 111), "variable nosuchvar not found");
}

TEST(StataTextErrorMessage, KeepsAMessageThatSpansLines)
{
    std::string output = "no; data in memory would be lost\nsecond line\nr(4);\n";
    EXPECT_EQ(errorMessage(output, 4), "no; data in memory would be lost\nsecond line");
}

TEST(StataTextErrorMessage, IsEmptyWithoutTheReturnCode)
{
    EXPECT_EQ(errorMessage("some output\n", 111), "");
    EXPECT_EQ(errorMessage("r(111);\n", 111), "");
    // A different code is not this error.
    EXPECT_EQ(errorMessage("message\nr(198);\n", 111), "");
}

TEST(StataTextTrim, RemovesSurroundingWhitespace)
{
    EXPECT_EQ(trim("  19.5\r\n"), "19.5");
    EXPECT_EQ(trim(""), "");
}
