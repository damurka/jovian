#include <gtest/gtest.h>
#include "datasuite/message.hpp"
#include "datasuite/json.hpp"

using namespace datasuite;

TEST(MessageTest, MessageBaseConstruction) {
    json header = {{"msg_id", "123"}, {"msg_type", "execute_request"}};
    json parent_header = {{"msg_id", "456"}};
    json metadata = {{"key", "value"}};
    json content = {{"code", "print('hello')"}};
    buffer_sequence buffers;
    buffers.push_back({'a', 'b', 'c'});

    message::guid_list zmq_id = {"id1"};
    message msg(zmq_id, header, parent_header, metadata, content, buffers);

    EXPECT_EQ(msg.header(), header);
    EXPECT_EQ(msg.parentHeader(), parent_header);
    EXPECT_EQ(msg.metadata(), metadata);
    EXPECT_EQ(msg.content(), content);
    EXPECT_EQ(msg.buffers().size(), 1);
    EXPECT_EQ(msg.buffers()[0].size(), 3);
}

TEST(MessageTest, MessageConstruction) {
    message::guid_list zmq_id = {"id1", "id2"};
    json header = {{"msg_id", "123"}};
    json parent_header = json::object();
    json metadata = json::object();
    json content = json::object();
    buffer_sequence buffers;

    message msg(zmq_id, header, parent_header, metadata, content, buffers);

    EXPECT_EQ(msg.identities(), zmq_id);
    EXPECT_EQ(msg.header(), header);
}

TEST(MessageTest, PubMessageConstruction) {
    std::string topic = "execute_result";
    json header = {{"msg_id", "123"}};
    json parent_header = json::object();
    json metadata = json::object();
    json content = json::object();
    buffer_sequence buffers;

    pub_message msg(topic, header, parent_header, metadata, content, buffers);

    EXPECT_EQ(msg.topic(), topic);
    EXPECT_EQ(msg.header(), header);
}

TEST(MessageTest, MakeHeader) {
    std::string msg_type = "execute_request";
    std::string user_name = "test_user";
    std::string session_id = "session_123";

    json header = makeHeader(msg_type, user_name, session_id);

    EXPECT_EQ(header["msg_type"], msg_type);
    EXPECT_EQ(header["username"], user_name);
    EXPECT_EQ(header["session"], session_id);
    EXPECT_TRUE(header.contains("msg_id"));
    EXPECT_TRUE(header.contains("date"));
    EXPECT_TRUE(header.contains("version"));
}
