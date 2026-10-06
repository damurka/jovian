// Coverage for native/src/adrastea/transport/common/zmq_serializer.cpp -- the wire
// format every shell/control/iopub message crosses through. Pure in-memory
// round trips against zmq::multipart_t: no socket, no network, no process.
// Previously zero direct coverage; only exercised indirectly via a full ZMQ
// round trip through SessionRegistryTest.
#include <gtest/gtest.h>

#include "adrastea/message.hpp"
#include "adrastea/transport/common/authentication.hpp"
#include "adrastea/transport/common/zmq_serializer.hpp"

using namespace adrastea;

namespace
{
    Message makeShellMessage()
    {
        json header = { { "msg_id", "abc123" }, { "msg_type", "execute_request" } };
        json parentHeader = json::object();
        json metadata = json::object();
        json content = { { "code", "1 + 1" } };
        buffer_sequence buffers;
        buffers.push_back({ 'x', 'y', 'z' });
        return Message({ "identity-1", "identity-2" }, header, parentHeader, metadata, content, buffers);
    }

    PubMessage makePubMessage()
    {
        json header = { { "msg_id", "def456" }, { "msg_type", "stream" } };
        json parentHeader = { { "msg_id", "abc123" } };
        json metadata = json::object();
        json content = { { "name", "stdout" }, { "text", "hello\n" } };
        return PubMessage("kernel_core.xyz.stream", header, parentHeader, metadata, content, buffer_sequence());
    }
}

TEST(ZmqSerializerTest, ShellMessageRoundTripsWithMatchingKey)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");

    zmq::multipart_t wire = ZmqSerializer::serialize(makeShellMessage(), *auth);
    Message result = ZmqSerializer::deserialize(wire, *auth);

    EXPECT_EQ(result.header().at("msg_id").get<std::string>(), "abc123");
    EXPECT_EQ(result.content().at("code").get<std::string>(), "1 + 1");
    EXPECT_EQ(result.identities(), (Message::guid_list{ "identity-1", "identity-2" }));
    ASSERT_EQ(result.buffers().size(), 1u);
    EXPECT_EQ(result.buffers()[0], (binary_buffer{ 'x', 'y', 'z' }));
}

TEST(ZmqSerializerTest, ShellMessageDeserializeThrowsWithWrongKey)
{
    auto signer = makeAuthentication("hmac-sha256", "key-one");
    auto verifier = makeAuthentication("hmac-sha256", "key-two");

    zmq::multipart_t wire = ZmqSerializer::serialize(makeShellMessage(), *signer);
    EXPECT_THROW(ZmqSerializer::deserialize(wire, *verifier), std::runtime_error);
}

TEST(ZmqSerializerTest, IopubMessageRoundTripsWithTopicAndParentHeader)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");

    zmq::multipart_t wire = ZmqSerializer::serializeIopub(makePubMessage(), *auth);
    PubMessage result = ZmqSerializer::deserializeIopub(wire, *auth);

    EXPECT_EQ(result.topic(), "kernel_core.xyz.stream");
    EXPECT_EQ(result.header().at("msg_type").get<std::string>(), "stream");
    EXPECT_EQ(result.parentHeader().at("msg_id").get<std::string>(), "abc123");
    EXPECT_EQ(result.content().at("text").get<std::string>(), "hello\n");
}

TEST(ZmqSerializerTest, IopubMessageDeserializeThrowsWithWrongKey)
{
    auto signer = makeAuthentication("hmac-sha256", "key-one");
    auto verifier = makeAuthentication("hmac-sha256", "key-two");

    zmq::multipart_t wire = ZmqSerializer::serializeIopub(makePubMessage(), *signer);
    EXPECT_THROW(ZmqSerializer::deserializeIopub(wire, *verifier), std::runtime_error);
}

TEST(ZmqSerializerTest, ZmqIdRoundTripsThroughTheDelimiter)
{
    Message::guid_list ids = { "route-a", "route-b" };
    zmq::multipart_t wire;
    ZmqSerializer::serializeZmqId(ids, wire);

    Message::guid_list result = ZmqSerializer::deserializeZmqId(wire);
    EXPECT_EQ(result, ids);
    EXPECT_TRUE(wire.empty());
}

TEST(ZmqSerializerTest, DeserializeZmqIdThrowsWhenDelimiterMissing)
{
    std::string notADelimiter = "not-a-delimiter";
    zmq::multipart_t wire;
    wire.add(zmq::message_t(notADelimiter.begin(), notADelimiter.end()));

    EXPECT_THROW(ZmqSerializer::deserializeZmqId(wire), std::runtime_error);
}

// ---- large content, kept as text for a reader that only passes it on (defer_large_content)

namespace
{
    // An iopub stream message as it is on the wire, with the content frame given as text: what a kernel that
    // writes its JSON some other way than ours would send.
    zmq::multipart_t wireWithContent(const Authentication& auth, const std::string& content)
    {
        const std::string topic = "stream";
        const std::string delimiter = "<IDS|MSG>";
        const std::string header = R"({"msg_id":"big-1","msg_type":"stream"})";
        const std::string parent = R"({"msg_id":"abc123"})";
        const std::string metadata = "{}";
        auto raw = [](const std::string& text) {
            return RawBuffer(reinterpret_cast<const unsigned char*>(text.data()), text.size());
        };
        const std::string signature = auth.sign(raw(header), raw(parent), raw(metadata), raw(content));
        zmq::multipart_t wire;
        for (const std::string* frame : { &topic, &delimiter, &signature, &header, &parent, &metadata, &content })
        {
            wire.add(zmq::message_t(frame->begin(), frame->end()));
        }
        return wire;
    }

    std::string streamContent(const std::string& text)
    {
        return json{ { "name", "stdout" }, { "text", text } }.dump();
    }
}

TEST(ZmqSerializerTest, LargeContentIsKeptAsItsTextWhenAskedAndParsedWhenRead)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    // not only ASCII: two-, three- and four-byte characters, and an escaped newline
    const std::string text = std::string(ZmqSerializer::kDeferredContentBytes, 'x') + "\n\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80";
    const std::string content = streamContent(text);

    zmq::multipart_t wire = wireWithContent(*auth, content);
    PubMessage result = ZmqSerializer::deserializeIopub(wire, *auth, true);

    ASSERT_NE(result.contentText(), nullptr);
    EXPECT_EQ(*result.contentText(), content) << "the text is the kernel's, byte for byte";
    EXPECT_EQ(result.header().at("msg_type").get<std::string>(), "stream");
    EXPECT_EQ(result.content().at("text").get<std::string>(), text) << "and it parses to the same content when read";
    ASSERT_NE(result.contentText(), nullptr) << "reading the content does not take the text away";
}

TEST(ZmqSerializerTest, ContentIsParsedAtOnceUnlessTheReaderAskedOtherwise)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    const std::string content = streamContent(std::string(ZmqSerializer::kDeferredContentBytes, 'x'));

    zmq::multipart_t wire = wireWithContent(*auth, content);
    PubMessage result = ZmqSerializer::deserializeIopub(wire, *auth);

    EXPECT_EQ(result.contentText(), nullptr);
    EXPECT_EQ(result.content().at("text").get<std::string>().size(), ZmqSerializer::kDeferredContentBytes);
}

TEST(ZmqSerializerTest, SmallContentIsParsedAtOnce)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");

    zmq::multipart_t wire = wireWithContent(*auth, streamContent("hello"));
    PubMessage result = ZmqSerializer::deserializeIopub(wire, *auth, true);

    EXPECT_EQ(result.contentText(), nullptr);
    EXPECT_EQ(result.content().at("text").get<std::string>(), "hello");
}

TEST(ZmqSerializerTest, LargeContentThatIsNotOneLineOfUtf8IsParsedAsBefore)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    const std::string filler(ZmqSerializer::kDeferredContentBytes, 'x');

    // JSON written over several lines is JSON all the same, but cannot go inside a one-line frame as it is
    {
        const std::string content = "{\n\t\"name\": \"stdout\",\n\t\"text\": \"" + filler + "\"\n}";
        zmq::multipart_t wire = wireWithContent(*auth, content);
        PubMessage result = ZmqSerializer::deserializeIopub(wire, *auth, true);
        EXPECT_EQ(result.contentText(), nullptr);
        EXPECT_EQ(result.content().at("text").get<std::string>(), filler);
    }

    // bytes that are not UTF-8 (a lone continuation byte, a truncated character, an overlong form, a surrogate):
    // a WebSocket text frame holding them would end the connection
    for (const std::string bad : { std::string("\x80"), std::string("\xE2\x82"), std::string("\xC0\xAF"), std::string("\xED\xA0\x80") })
    {
        const std::string content = "{\"name\":\"stdout\",\"text\":\"" + filler + bad + "\"}";
        zmq::multipart_t wire = wireWithContent(*auth, content);
        PubMessage result = ZmqSerializer::deserializeIopub(wire, *auth, true);
        EXPECT_EQ(result.contentText(), nullptr) << "kept as text with bytes that are not UTF-8";
    }
}

TEST(ZmqSerializerTest, LargeContentIsStillRefusedWhenItsSignatureIsWrong)
{
    auto signer = makeAuthentication("hmac-sha256", "key-one");
    auto verifier = makeAuthentication("hmac-sha256", "key-two");

    zmq::multipart_t wire = wireWithContent(*signer, streamContent(std::string(ZmqSerializer::kDeferredContentBytes, 'x')));
    EXPECT_THROW(ZmqSerializer::deserializeIopub(wire, *verifier, true), std::runtime_error);
}

TEST(ZmqSerializerTest, AShellReplysLargeContentIsKeptAsItsTextToo)
{
    auto auth = makeAuthentication("hmac-sha256", "shared-secret");
    json header = { { "msg_id", "r1" }, { "msg_type", "execute_reply" } };
    json content = { { "status", "ok" }, { "payload", std::string(ZmqSerializer::kDeferredContentBytes, 'y') } };
    zmq::multipart_t wire = ZmqSerializer::serialize(
        Message({ "identity-1" }, header, json::object(), json::object(), content, buffer_sequence()), *auth);

    Message result = ZmqSerializer::deserialize(wire, *auth, true);

    ASSERT_NE(result.contentText(), nullptr);
    EXPECT_EQ(*result.contentText(), content.dump());
    EXPECT_EQ(result.content().at("status").get<std::string>(), "ok");
}
