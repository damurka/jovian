#include "zmq_serializer.hpp"
#include "datasuite/json.hpp"

namespace datasuite
{
    namespace
    {
        const std::string DELIMITER = "<IDS|MSG>";

        bool isDelimiter(zmq::message_t& frame)
        {
            std::size_t frame_size = frame.size();
            if (frame_size != DELIMITER.size())
            {
                return false;
            }

            std::string check(frame.data<const char>(), frame_size);
            return check == DELIMITER;
        }

        RawBuffer makeRawBuffer(zmq::message_t& msg)
        {
            return ZmqSerializer::makeRawBuffer(msg);
        }

        void parseZmqMessage(const zmq::message_t& msg, json& j)
        {
            const char* buf = msg.data<const char>();
            try {
                // Use the ignore_cb parameter to ignore invalid UTF-8 characters
                j = json::parse(buf, buf + msg.size(), nullptr, false, true);
            } catch (const json::parse_error& e) {
                // If parsing still fails, create a fallback JSON object
                j = json::object();
                j["error"] = "JSON parse error: " + std::string(e.what());

                // Try to extract whatever we can as a raw string, replacing invalid chars
                std::string raw_str;
                for (size_t i = 0; i < msg.size(); ++i) {
                    unsigned char c = buf[i];
                    if (c < 128) {
                        raw_str += c;
                    } else {
                        raw_str += '?'; // Replace invalid UTF-8 with question mark
                    }
                }
                j["raw_content"] = raw_str;
            }
        }

        zmq::message_t writeZmqMessage(const json& json, json::error_handler_t error_handler)
        {
            std::string buffer = json.dump(-1, ' ', false, error_handler);
            return zmq::message_t(buffer.c_str(), buffer.size());
        }

        void serializeMessageBase(MessageBase&& msg,
            const Authentication& auth,
            json::error_handler_t error_handler,
            zmq::multipart_t& wire_msg)
        {
            zmq::message_t header = writeZmqMessage(msg.header(), error_handler);
            zmq::message_t parent_header = writeZmqMessage(msg.parentHeader(), error_handler);
            zmq::message_t metadata = writeZmqMessage(msg.metadata(), error_handler);
            zmq::message_t content = writeZmqMessage(msg.content(), error_handler);
            std::string sig = auth.sign(makeRawBuffer(header),
                makeRawBuffer(parent_header),
                makeRawBuffer(metadata),
                makeRawBuffer(content));
            zmq::message_t signature(sig.begin(), sig.end());

            wire_msg.add(std::move(signature));
            wire_msg.add(std::move(header));
            wire_msg.add(std::move(parent_header));
            wire_msg.add(std::move(metadata));
            wire_msg.add(std::move(content));

            // was not const and  could only be called on rvalues.
            for (const binary_buffer& buffer : std::move(msg).buffers())
            {
                wire_msg.add(zmq::message_t(buffer.data(), buffer.size()));
            }
        }

        std::tuple<json, json, json, json, buffer_sequence>  deserializeMessageBase(zmq::multipart_t& wire_msg,
            const Authentication& auth)
        {
            zmq::message_t signature = wire_msg.pop();
            zmq::message_t header = wire_msg.pop();
            zmq::message_t parent_header = wire_msg.pop();
            zmq::message_t metadata = wire_msg.pop();
            zmq::message_t content = wire_msg.pop();

            json j_header, j_parent_header, j_metadata, j_content;
            parseZmqMessage(header, j_header);
            parseZmqMessage(parent_header, j_parent_header);
            parseZmqMessage(metadata, j_metadata);
            parseZmqMessage(content, j_content);

            buffer_sequence buffers;
            while (!wire_msg.empty())
            {
                zmq::message_t msg = wire_msg.pop();
                const char* buf = msg.data<const char>();
                buffers.emplace_back(buf, buf + msg.size());
            }

            // TODO: should we verify with buffers
            if (!auth.verify(makeRawBuffer(signature),
                makeRawBuffer(header),
                makeRawBuffer(parent_header),
                makeRawBuffer(metadata),
                makeRawBuffer(content)))
            {
                throw std::runtime_error("ERROR: Signatures don't match");
            }

            return {
                std::move(j_header),
                std::move(j_parent_header),
                std::move(j_metadata),
                std::move(j_content),
                std::move(buffers)
            };
        }

        void serializeTopic(const PubMessage& msg, zmq::multipart_t& wire_msg)
        {
            wire_msg.add(zmq::message_t(msg.topic().begin(), msg.topic().end()));
            wire_msg.add(zmq::message_t(DELIMITER.begin(), DELIMITER.end()));
        }

        std::string deserializeTopic(zmq::multipart_t& wire_msg)
        {
            zmq::message_t topic_msg = wire_msg.pop();
            std::string topic = std::string(topic_msg.data<const char>(), topic_msg.size());
            wire_msg.pop();
            return topic;
        }
    }

    zmq::multipart_t ZmqSerializer::serialize(Message&& msg,
        const Authentication& auth,
        json::error_handler_t error_handler)
    {
        zmq::multipart_t wire_msg;
        serializeZmqId(msg.identities(), wire_msg);
        serializeMessageBase(std::move(msg), auth, error_handler, wire_msg);
        return wire_msg;
    }

    Message ZmqSerializer::deserialize(zmq::multipart_t& wire_msg,
        const Authentication& auth)
    {
        Message::guid_list zmq_id = deserializeZmqId(wire_msg);
        auto [header, parent_header, metadata, content, buffers] = deserializeMessageBase(wire_msg, auth);
        return Message(
            std::move(zmq_id),
            std::move(header),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers)
        );
    }

    zmq::multipart_t ZmqSerializer::serializeIopub(PubMessage&& msg,
        const Authentication& auth,
        json::error_handler_t error_handler)
    {
        zmq::multipart_t wire_msg;
        serializeTopic(msg, wire_msg);
        serializeMessageBase(std::move(msg), auth, error_handler, wire_msg);
        return wire_msg;
    }

    PubMessage ZmqSerializer::deserializeIopub(zmq::multipart_t& wire_msg,
        const Authentication& auth)
    {
        std::string topic = deserializeTopic(wire_msg);
        auto [header, parent_header, metadata, content, buffers] = deserializeMessageBase(wire_msg, auth);
        return PubMessage(topic,
            std::move(header),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers)
        );
    }

    void ZmqSerializer::serializeZmqId(const Message::guid_list& ids, zmq::multipart_t& wire_msg)
    {
        auto app = [&wire_msg](const std::string& uid) {
            wire_msg.add(zmq::message_t(uid.begin(), uid.end()));
            };
        std::for_each(ids.begin(), ids.end(), app);
        wire_msg.add(zmq::message_t(DELIMITER.begin(), DELIMITER.end()));
    }

    Message::guid_list ZmqSerializer::deserializeZmqId(zmq::multipart_t& wire_msg)
    {
        Message::guid_list zmq_id;
        zmq::message_t frame = wire_msg.pop();
        bool foundDelimiter = isDelimiter(frame);

        // ZMQ identities
        while (!foundDelimiter && wire_msg.size() != 0)
        {
            zmq_id.emplace_back(frame.data<const char>(), frame.size());
            frame = wire_msg.pop();
            foundDelimiter = isDelimiter(frame);
        }

        // Whether the delimiter was ever found -- not whether wire_msg is
        // now empty, which used to be conflated here (see
        // native/test/zmq_serializer_test.cpp): if the delimiter happened
        // to be the *last* frame in wire_msg (nothing following it, e.g. a
        // caller testing serializeZmqId()/deserializeZmqId() in isolation
        // rather than as a prefix of a full serialize()'d message), popping
        // it also drains wire_msg to empty, and the old check treated that
        // successful parse as "delimiter not present".
        if (!foundDelimiter)
        {
            throw std::runtime_error("ERROR: Delimiter not present in message");
        }
        return zmq_id;
    }

    RawBuffer ZmqSerializer::makeRawBuffer(zmq::message_t& msg)
    {
        return RawBuffer(msg.data<const unsigned char>(), msg.size());
    }
}
