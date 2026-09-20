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
