// Who may use the supervisor's API (themisto/access.hpp): the token, and no
// requests from web pages.
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "themisto/access.hpp"

using namespace themisto::access;

TEST(AccessTest, NewTokensAreLongRandomAndDifferent)
{
    std::set<std::string> tokens;
    for (int i = 0; i < 50; ++i)
    {
        std::string token = newToken();
        EXPECT_EQ(token.size(), 64u);
        EXPECT_EQ(token.find_first_not_of("0123456789abcdef"), std::string::npos) << token;
        tokens.insert(token);
    }
    EXPECT_EQ(tokens.size(), 50u);
}

TEST(AccessTest, TokenMatchesOnlyTheExactToken)
{
    EXPECT_TRUE(tokenMatches("abc123", "abc123"));
    EXPECT_FALSE(tokenMatches("abc124", "abc123"));
    EXPECT_FALSE(tokenMatches("abc12", "abc123"));
    EXPECT_FALSE(tokenMatches("", "abc123"));
    // An unset token never matches -- not even an empty one.
    EXPECT_FALSE(tokenMatches("", ""));
}

TEST(AccessTest, BearerTokenIsReadFromTheAuthorizationHeader)
{
    EXPECT_EQ(bearerToken("Bearer abc").value_or("-"), "abc");
    EXPECT_EQ(bearerToken("bearer  abc ").value_or("-"), "abc");
    EXPECT_FALSE(bearerToken("Basic abc").has_value());
    EXPECT_FALSE(bearerToken("Bearer ").has_value());
    EXPECT_FALSE(bearerToken("").has_value());
}

TEST(AccessTest, QueryParameterIsFoundAndDecoded)
{
    EXPECT_EQ(queryParameter("/sessions/x/messages?token=abc", "token").value_or("-"), "abc");
    EXPECT_EQ(queryParameter("/a?x=1&token=a%2Bb&y=2", "token").value_or("-"), "a+b");
    EXPECT_FALSE(queryParameter("/a?tokens=abc", "token").has_value());
    EXPECT_FALSE(queryParameter("/a", "token").has_value());
    EXPECT_EQ(pathOf("/sessions/x/messages?token=abc"), "/sessions/x/messages");
}

TEST(AccessTest, CheckAllowsTheTokenInEitherPlace)
{
    const std::string token = "secret";
    EXPECT_EQ(check("", "Bearer secret", "/sessions", token), Verdict::Allowed);
    EXPECT_EQ(check("", "", "/sessions/x/messages?token=secret", token), Verdict::Allowed);
}

TEST(AccessTest, CheckRefusesAMissingOrWrongToken)
{
    const std::string token = "secret";
    EXPECT_EQ(check("", "", "/sessions", token), Verdict::BadToken);
    EXPECT_EQ(check("", "Bearer nope", "/sessions?token=nope", token), Verdict::BadToken);
}

TEST(AccessTest, CheckRefusesWebPagesEvenWithTheToken)
{
    // A page could learn the token some other way; it still may not drive kernels.
    EXPECT_EQ(check("http://evil.example", "Bearer secret", "/sessions", "secret"), Verdict::FromBrowser);
    EXPECT_EQ(check("null", "", "/sessions", ""), Verdict::FromBrowser);
}

TEST(AccessTest, AnEmptyTokenTurnsTheTokenCheckOff)
{
    EXPECT_EQ(check("", "", "/sessions", ""), Verdict::Allowed);
}
