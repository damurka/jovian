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

TEST(StataTextCompletion, TheFirstWordCompletesCommands)
{
    EXPECT_EQ(completionToken("reg", 3).kind, CompletionKind::Command);
    EXPECT_EQ(completionToken("sysuse auto\n  summ", 18).kind, CompletionKind::Command);
    EXPECT_EQ(completionToken("quietly summ", 12).kind, CompletionKind::Command);
    EXPECT_EQ(completionToken("by foreign: tab", 15).kind, CompletionKind::Command);
    EXPECT_EQ(completionToken("capture noisily reg", 19).kind, CompletionKind::Command);
    // not the first word
    EXPECT_EQ(completionToken("summarize pri", 13).kind, CompletionKind::Variable);
}

TEST(StataTextCompletion, StoredResultsInsideRAndE)
{
    CompletionToken token = completionToken("display r(me", 12);
    EXPECT_EQ(token.kind, CompletionKind::Result);
    EXPECT_EQ(token.resultClass, 'r');
    EXPECT_EQ(token.prefix, "me");
    EXPECT_EQ(completionToken("display e(", 10).resultClass, 'e');
    // a function whose name ends in r is not r()
    EXPECT_NE(completionToken("display floor(x", 15).kind, CompletionKind::Result);
}

TEST(StataTextPrintedLines, JoinsWrappedLinesAndDropsBlankOnes)
{
    EXPECT_EQ(unwrap("C:/Program Files/Stata18/utilities/jav\n> a/windows-x64\n"), "C:/Program Files/Stata18/utilities/java/windows-x64\n");
    EXPECT_EQ(printedLines("\nmake\r\nprice\n\n  mpg \n"), (std::vector<std::string>{ "make", "price", "mpg" }));
}

TEST(StataTextSingleCommand, OnlyOnePlainCompleteLine)
{
    EXPECT_TRUE(isSingleCommand("display `answer' + _N"));
    EXPECT_TRUE(isSingleCommand("  pwd\n"));
    EXPECT_FALSE(isSingleCommand("display 1\ndisplay 2"));
    EXPECT_FALSE(isSingleCommand("forvalues i = 1/3 {"));
    EXPECT_FALSE(isSingleCommand("display 1 // a comment"));
    EXPECT_FALSE(isSingleCommand("* a comment"));
    EXPECT_FALSE(isSingleCommand("#delimit ;"));
    EXPECT_FALSE(isSingleCommand("exit"));
    EXPECT_FALSE(isSingleCommand(""));
}

TEST(StataTextUtf8, KeepsValidTextAndHoldsASplitCharacter)
{
    Utf8Decoder decoder;
    EXPECT_EQ(decoder.push("caf\xC3"), "caf");
    EXPECT_EQ(decoder.push("\xA9 ok"), "\xC3\xA9 ok");
    EXPECT_EQ(decoder.finish(), "");
}

TEST(StataTextUtf8, ReadsBytesThatAreNotUtf8AsLatin1)
{
    Utf8Decoder decoder;
    // "café" saved by Stata 13 (Latin-1)
    EXPECT_EQ(decoder.push("caf\xE9 x"), "caf\xC3\xA9 x");
    // a sequence cut off at the end of the output
    EXPECT_EQ(decoder.push("\xE2\x82"), "");
    EXPECT_EQ(decoder.finish(), "\xC3\xA2\xC2\x82");
}

namespace
{
    // What an EchoFilter makes of `output` given in chunks of `size` bytes
    std::string filtered(const std::string& output, std::size_t size)
    {
        EchoFilter filter;
        std::string shown;
        for (std::size_t i = 0; i < output.size(); i += size) shown += filter.push(output.substr(i, size));
        return shown + filter.finish();
    }

    void expectFiltered(const std::string& output, const std::string& expected)
    {
        for (std::size_t size : { std::size_t(1), std::size_t(3), std::size_t(7), output.size() })
        {
            EXPECT_EQ(filtered(output, size), expected) << "in chunks of " << size;
        }
    }
}

TEST(StataTextEchoFilter, RemovesTheCommandsADoFileEchoes)
{
    expectFiltered("\n. sysuse auto, clear\n(1978 automobile data)\n\n. local answer = 42\n\n. ", "(1978 automobile data)\n");
    expectFiltered("\n. display 1\n1\n\n. display 2\n2\n\n. ", "1\n2\n");
    expectFiltered("\n. pwd\nC:\\Users\\me\n\n. ", "C:\\Users\\me\n");
}

TEST(StataTextEchoFilter, RemovesContinuationsAndTheLinesOfABlock)
{
    expectFiltered("\n. regress price mpg weight length turn displacement gear_ratio foreign headroom\n>  trunk rep78\n\nLinear regression\n\n. ",
        "\nLinear regression\n");
    expectFiltered("\n. forvalues i = 1/3 {\n  2.     display `i'\n  3.     if `i' == 2 {\n  4.         display \"two\"\n  5.     }\n  6. }\n1\n2\ntwo\n3\n\n. display \"after\"\nafter\n\n. ",
        "1\n2\ntwo\n3\nafter\n");
    expectFiltered("\n. program define hello\n  1.     display \"hello\"\n  2. end\n\n. hello\nhello\n\n. ", "hello\n");
    expectFiltered("\n. * a comment\n. // another\n. display \"x\" // trailing\nx\n\n. ", "x\n");
}

TEST(StataTextEchoFilter, KeepsOutputThatLooksNumberedOutsideABlock)
{
    // list's rows are numbered too
    expectFiltered("\n. list make in 1/2\n\n     +-------------+\n     | make        |\n  1. | AMC Concord |\n\n. ",
        "\n     +-------------+\n     | make        |\n  1. | AMC Concord |\n");
}

TEST(StataTextEchoFilter, RemovesAWrappedLineOfAProgram)
{
    // a line of a block longer than c(linesize) goes on in "> " lines
    expectFiltered("\n. program define dslist\n  1.     display as text \"This notebook's dataset (x) and what is saved in its\n> workspace:\"\n  2. end\n\n. dslist\nThis notebook's dataset\n\n. ",
        "This notebook's dataset\n");
}

TEST(StataTextTailHold, KeepsTheLastLinesUntilTheEnd)
{
    TailHold hold;
    std::string out;
    for (int i = 1; i <= 10; ++i) out += hold.push("line " + std::to_string(i) + "\n");
    EXPECT_EQ(out, "line 1\nline 2\n");
    EXPECT_EQ(hold.finish(""), "line 3\nline 4\nline 5\nline 6\nline 7\nline 8\nline 9\nline 10\n");
}

TEST(StataTextTailHold, LeavesOutTheErrorItShowsAsTheError)
{
    TailHold hold;
    EXPECT_EQ(hold.push("1\nvariable nosuchvar not found\nr(111);\n"), "");
    EXPECT_EQ(hold.finish("variable nosuchvar not found\nr(111);"), "1\n");

    // only a whole trailing error is taken out
    TailHold other;
    other.push("not the error\n");
    EXPECT_EQ(other.finish("variable nosuchvar not found\nr(111);"), "not the error\n");
}

TEST(StataTextTailHold, ReleasesWhatItHolds)
{
    TailHold hold;
    EXPECT_EQ(hold.push("Iteration 0\n"), "");
    EXPECT_EQ(hold.release(), "Iteration 0\n");
    EXPECT_EQ(hold.finish(""), "");
}

TEST(StataTextEchoFilter, ShowsAReturnCodeOnce)
{
    expectFiltered("\n. display 1\n1\n\n. regress price nosuchvar\nvariable nosuchvar not found\nr(111);\nr(111);\n",
        "1\nvariable nosuchvar not found\nr(111);\n");
}

TEST(StataTextEchoFilter, OutputGoesOutBeforeItsLineEnds)
{
    EchoFilter filter;
    filter.push("\n. _dots 0\n");
    // _dots' progress: no echo starts with ".."
    EXPECT_EQ(filter.push(".........."), "..........");
    EXPECT_EQ(filter.push(".."), "..");
    EchoFilter progress;
    EXPECT_EQ(progress.push("\n. sleep 10\nIteration 0"), "Iteration 0");
    EXPECT_EQ(progress.push(": done\n"), ": done\n");
}
