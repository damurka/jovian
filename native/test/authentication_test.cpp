// Coverage for native/src/transport/common/authentication.cpp -- the HMAC
// signing every wire message and the registration handshake rely on
// (ClientMessenger, ClientHandshakeZmq, ZmqSerializer) had zero direct
// tests before this; everything exercising it did so only indirectly
// through a full ZMQ round trip.
#include <string>

#include <gtest/gtest.h>

#include "transport/common/authentication.hpp"

using namespace adrastea;

namespace
{
    RawBuffer toBuffer(const std::string& s)
    {
        return RawBuffer(reinterpret_cast<const unsigned char*>(s.data()), s.size());
    }
}

TEST(AuthenticationTest, SignThenVerifyWithSameKeySucceeds)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    std::string content = "hello world";

    std::string signature = auth->sign(toBuffer(content));
    EXPECT_TRUE(auth->verify(toBuffer(signature), toBuffer(content)));
}

TEST(AuthenticationTest, VerifyFailsWithDifferentKey)
{
    auto signer = makeAuthentication("hmac-sha256", "key-one");
    auto verifier = makeAuthentication("hmac-sha256", "key-two");
    std::string content = "hello world";

    std::string signature = signer->sign(toBuffer(content));
    EXPECT_FALSE(verifier->verify(toBuffer(signature), toBuffer(content)));
}

TEST(AuthenticationTest, VerifyFailsWhenContentIsTampered)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    std::string original = "the real message";
    std::string tampered = "the real message!";

    std::string signature = auth->sign(toBuffer(original));
    EXPECT_FALSE(auth->verify(toBuffer(signature), toBuffer(tampered)));
}

TEST(AuthenticationTest, MultiBufferSignThenVerifySucceeds)
{
    // The four-buffer overload is what actually signs wire messages
    // (header/parent_header/metadata/content, see zmq_serializer.cpp) --
    // the single-buffer overload above is only used for the registration
    // handshake's plain JSON payload (handshaking.cpp).
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    std::string header = R"({"msg_id":"1"})";
    std::string parentHeader = "{}";
    std::string metadata = "{}";
    std::string content = R"({"code":"1+1"})";

    std::string signature = auth->sign(toBuffer(header), toBuffer(parentHeader), toBuffer(metadata), toBuffer(content));
    EXPECT_TRUE(auth->verify(
        toBuffer(signature), toBuffer(header), toBuffer(parentHeader), toBuffer(metadata), toBuffer(content)));
}

TEST(AuthenticationTest, MultiBufferVerifyFailsWhenOneFieldChanges)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    std::string header = R"({"msg_id":"1"})";
    std::string parentHeader = "{}";
    std::string metadata = "{}";
    std::string content = R"({"code":"1+1"})";

    std::string signature = auth->sign(toBuffer(header), toBuffer(parentHeader), toBuffer(metadata), toBuffer(content));

    std::string differentContent = R"({"code":"2+2"})";
    EXPECT_FALSE(auth->verify(
        toBuffer(signature), toBuffer(header), toBuffer(parentHeader), toBuffer(metadata), toBuffer(differentContent)));
}

TEST(AuthenticationTest, NoneSchemeAlwaysSignsEmptyAndVerifiesAnything)
{
    // makeAuthentication("none", ...) (authentication.cpp's NoAuthentication)
    // is what an unauthenticated connection file (no "signature_scheme")
    // resolves to via KernelConfiguration -- never exercised until now,
    // since every other test in this suite uses "hmac-sha256".
    auto auth = makeAuthentication("none", "");
    std::string content = "anything at all";

    EXPECT_EQ(auth->sign(toBuffer(content)), "");
    EXPECT_TRUE(auth->verify(toBuffer("this signature is ignored"), toBuffer(content)));

    std::string header = R"({"msg_id":"1"})";
    std::string parentHeader = "{}";
    std::string metadata = "{}";
    EXPECT_EQ(auth->sign(toBuffer(header), toBuffer(parentHeader), toBuffer(metadata), toBuffer(content)), "");
    EXPECT_TRUE(auth->verify(
        toBuffer("ignored"), toBuffer(header), toBuffer(parentHeader), toBuffer(metadata), toBuffer(content)));
}
