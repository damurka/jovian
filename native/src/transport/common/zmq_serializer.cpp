#include "zmq_serializer.hpp"
#include "datasuite/json.hpp"

namespace datasuite
{
    namespace
    {
        const std::string DELIMITER = "<IDS|MSG>";

        bool is_delimiter(zmq::message_t& frame)
        {
            std::size_t frame_size = frame.size();
            if (frame_size != DELIMITER.size())
            {
                return false;
            }

            std::string check(frame.data<const char>(), frame_size);
            return check == DELIMITER;
        }

        raw_buffer make_raw_buffer(zmq::message_t& msg)
        {
            return zmq_serializer::make_raw_buffer(msg);
        }

        void parse_zmq_message(const zmq::message_t& msg, json& j)
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

        zmq::message_t write_zmq_message(const json& json, json::error_handler_t error_handler)
        {
            std::string buffer = json.dump(-1, ' ', false, error_handler);
            return zmq::message_t(buffer.c_str(), buffer.size());
        }

        void serialize_message_base(message_base&& msg,
            const authentication& auth,
            json::error_handler_t error_handler,
            zmq::multipart_t& wire_msg)
        {
            zmq::message_t header = write_zmq_message(msg.header(), error_handler);
            zmq::message_t parent_header = write_zmq_message(msg.parent_header(), error_handler);
            zmq::message_t metadata = write_zmq_message(msg.metadata(), error_handler);
            zmq::message_t content = write_zmq_message(msg.content(), error_handler);
            std::string sig = auth.sign(make_raw_buffer(header),
                make_raw_buffer(parent_header),
                make_raw_buffer(metadata),
                make_raw_buffer(content));
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

        std::tuple<json, json, json, json, buffer_sequence>  deserialize_message_base(zmq::multipart_t& wire_msg,
            const authentication& auth)
        {
            zmq::message_t signature = wire_msg.pop();
            zmq::message_t header = wire_msg.pop();
            zmq::message_t parent_header = wire_msg.pop();
            zmq::message_t metadata = wire_msg.pop();
            zmq::message_t content = wire_msg.pop();

            json j_header, j_parent_header, j_metadata, j_content;
            parse_zmq_message(header, j_header);
            parse_zmq_message(parent_header, j_parent_header);
            parse_zmq_message(metadata, j_metadata);
            parse_zmq_message(content, j_content);

            buffer_sequence buffers;
            while (!wire_msg.empty())
            {
                zmq::message_t msg = wire_msg.pop();
                const char* buf = msg.data<const char>();
                buffers.emplace_back(buf, buf + msg.size());
            }

            // TODO: should we verify with buffers
            if (!auth.verify(make_raw_buffer(signature),
                make_raw_buffer(header),
                make_raw_buffer(parent_header),
                make_raw_buffer(metadata),
                make_raw_buffer(content)))
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

        void serialize_topic(const pub_message& msg, zmq::multipart_t& wire_msg)
        {
            wire_msg.add(zmq::message_t(msg.topic().begin(), msg.topic().end()));
            wire_msg.add(zmq::message_t(DELIMITER.begin(), DELIMITER.end()));
        }

        std::string deserialize_topic(zmq::multipart_t& wire_msg)
        {
            zmq::message_t topic_msg = wire_msg.pop();
            std::string topic = std::string(topic_msg.data<const char>(), topic_msg.size());
            wire_msg.pop();
            return topic;
        }
    }

    zmq::multipart_t zmq_serializer::serialize(message&& msg,
        const authentication& auth,
        json::error_handler_t error_handler)
    {
        zmq::multipart_t wire_msg;
        serialize_zmq_id(msg.identities(), wire_msg);
        serialize_message_base(std::move(msg), auth, error_handler, wire_msg);
        return wire_msg;
    }

    message zmq_serializer::deserialize(zmq::multipart_t& wire_msg,
        const authentication& auth)
    {
        message::guid_list zmq_id = deserialize_zmq_id(wire_msg);
        auto [header, parent_header, metadata, content, buffers] = deserialize_message_base(wire_msg, auth);
        return message(
            std::move(zmq_id), 
            std::move(header), 
            std::move(parent_header), 
            std::move(metadata), 
            std::move(content), 
            std::move(buffers)
        );
    }

    zmq::multipart_t zmq_serializer::serialize_iopub(pub_message&& msg,
        const authentication& auth,
        json::error_handler_t error_handler)
    {
        zmq::multipart_t wire_msg;
        serialize_topic(msg, wire_msg);
        serialize_message_base(std::move(msg), auth, error_handler, wire_msg);
        return wire_msg;
    }

    pub_message zmq_serializer::deserialize_iopub(zmq::multipart_t& wire_msg,
        const authentication& auth)
    {
        std::string topic = deserialize_topic(wire_msg);
        auto [header, parent_header, metadata, content, buffers] = deserialize_message_base(wire_msg, auth);
        return pub_message(topic, 
            std::move(header), 
            std::move(parent_header), 
            std::move(metadata), 
            std::move(content), 
            std::move(buffers)
        );
    }

    void zmq_serializer::serialize_zmq_id(const message::guid_list& ids, zmq::multipart_t& wire_msg)
    {
        auto app = [&wire_msg](const std::string& uid) {
            wire_msg.add(zmq::message_t(uid.begin(), uid.end()));
            };
        std::for_each(ids.begin(), ids.end(), app);
        wire_msg.add(zmq::message_t(DELIMITER.begin(), DELIMITER.end()));
    }

    message::guid_list zmq_serializer::deserialize_zmq_id(zmq::multipart_t& wire_msg)
    {
        message::guid_list zmq_id;
        zmq::message_t frame = wire_msg.pop();

        // ZMQ identites
        while (!is_delimiter(frame) && wire_msg.size() != 0)
        {
            zmq_id.emplace_back(frame.data<const char>(), frame.size());
            frame = wire_msg.pop();
        }

        // if wire_msg is empty, that means frame doesn't contain <IDS|MSG>
        if (wire_msg.size() == 0)
        {
            throw std::runtime_error("ERROR: Delimiter not present in message");
        }
        return zmq_id;
    }

    raw_buffer zmq_serializer::make_raw_buffer(zmq::message_t& msg)
    {
        return raw_buffer(msg.data<const unsigned char>(), msg.size());
    }
}
