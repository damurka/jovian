#include <cstdint>
#include <cstring>
#include "zmq_serializer.hpp"
#include "adrastea/json.hpp"
#include <stdexcept>
#include <string>

namespace adrastea
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

        // Whether a text is valid UTF-8 with no control character in it (so: on one line). JSON text that passes
        // can be put inside another JSON text, and sent in a WebSocket text frame, as it is. A text that fails is
        // not wrong, only parsed and written again as before: JSON may have newlines and tabs between its tokens.
        bool isOneLineOfUtf8(const unsigned char* p, std::size_t n)
        {
            std::size_t i = 0;
            while (i < n)
            {
                // eight bytes at a time while they are ASCII and none is below 0x20
                while (i + 8 <= n)
                {
                    std::uint64_t v;
                    std::memcpy(&v, p + i, 8);
                    if ((v & 0x8080808080808080ull) != 0 || ((v - 0x2020202020202020ull) & ~v & 0x8080808080808080ull) != 0)
                    {
                        break;
                    }
                    i += 8;
                }
                if (i >= n)
                {
                    break;
                }
                const unsigned char c = p[i];
                if (c < 0x20)
                {
                    return false;
                }
                if (c < 0x80)
                {
                    ++i;
                    continue;
                }
                std::size_t length;
                std::uint32_t code, smallest;
                if ((c & 0xE0) == 0xC0) { length = 2; code = c & 0x1Fu; smallest = 0x80; }
                else if ((c & 0xF0) == 0xE0) { length = 3; code = c & 0x0Fu; smallest = 0x800; }
                else if ((c & 0xF8) == 0xF0) { length = 4; code = c & 0x07u; smallest = 0x10000; }
                else { return false; }
                if (i + length > n)
                {
                    return false;
                }
                for (std::size_t k = 1; k < length; ++k)
                {
                    const unsigned char next = p[i + k];
                    if ((next & 0xC0) != 0x80)
                    {
                        return false;
                    }
                    code = (code << 6) | (next & 0x3Fu);
                }
                // overlong forms, the surrogates, and beyond Unicode
                if (code < smallest || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                {
                    return false;
                }
                i += length;
            }
            return true;
        }

        struct MessageParts
        {
            json header;
            json parent_header;
            json metadata;
            json content;
            std::string content_text; // in place of content, when it was deferred
            bool content_deferred = false;
            buffer_sequence buffers;
        };

        MessageParts deserializeMessageBase(zmq::multipart_t& wire_msg,
            const Authentication& auth,
            bool defer_large_content)
        {
            // Popping an empty multipart hands back an invalid message that
            // ZeroMQ asserts on (aborting the process) the moment it is read.
            if (wire_msg.size() < 5)
            {
                throw std::runtime_error("malformed message: " + std::to_string(wire_msg.size()) +
                                         " frames after the delimiter, 5 needed");
            }
            zmq::message_t signature = wire_msg.pop();
            zmq::message_t header = wire_msg.pop();
            zmq::message_t parent_header = wire_msg.pop();
            zmq::message_t metadata = wire_msg.pop();
            zmq::message_t content = wire_msg.pop();

            json j_header, j_parent_header, j_metadata, j_content;
            parseZmqMessage(header, j_header);
            parseZmqMessage(parent_header, j_parent_header);
            parseZmqMessage(metadata, j_metadata);
            const bool deferred = defer_large_content
                && content.size() >= ZmqSerializer::kDeferredContentBytes
                && isOneLineOfUtf8(content.data<const unsigned char>(), content.size());
            if (!deferred)
            {
                parseZmqMessage(content, j_content);
            }

            buffer_sequence buffers;
            while (!wire_msg.empty())
            {
                zmq::message_t msg = wire_msg.pop();
                const char* buf = msg.data<const char>();
                buffers.emplace_back(buf, buf + msg.size());
            }

            // The signature covers the header, parent header, metadata and content, and not the buffers: that is
            // the Jupyter wire protocol's definition of it, and what every frontend and kernel computes.
            if (!auth.verify(makeRawBuffer(signature),
                makeRawBuffer(header),
                makeRawBuffer(parent_header),
                makeRawBuffer(metadata),
                makeRawBuffer(content)))
            {
                throw std::runtime_error("ERROR: Signatures don't match");
            }

            MessageParts parts;
            parts.header = std::move(j_header);
            parts.parent_header = std::move(j_parent_header);
            parts.metadata = std::move(j_metadata);
            parts.content = std::move(j_content);
            parts.buffers = std::move(buffers);
            if (deferred)
            {
                parts.content_text.assign(content.data<const char>(), content.size());
                parts.content_deferred = true;
            }
            return parts;
        }

        void serializeTopic(const PubMessage& msg, zmq::multipart_t& wire_msg)
        {
            wire_msg.add(zmq::message_t(msg.topic().begin(), msg.topic().end()));
            wire_msg.add(zmq::message_t(DELIMITER.begin(), DELIMITER.end()));
        }

        // The frames before <IDS|MSG>: zero or more (the Jupyter spec allows
        // any number of topic frames; Adrastea's kernels send one, Ark sends
        // none on some messages). The topic is the first, "" when there is
        // none; the delimiter is consumed.
        std::string deserializeTopic(zmq::multipart_t& wire_msg)
        {
            std::string topic;
            bool first = true;
            while (!wire_msg.empty())
            {
                zmq::message_t frame = wire_msg.pop();
                if (isDelimiter(frame))
                {
                    return topic;
                }
                if (first)
                {
                    topic.assign(frame.data<const char>(), frame.size());
                    first = false;
                }
            }
            throw std::runtime_error("malformed message: no <IDS|MSG> delimiter");
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
        const Authentication& auth,
        bool defer_large_content)
    {
        Message::guid_list zmq_id = deserializeZmqId(wire_msg);
        MessageParts parts = deserializeMessageBase(wire_msg, auth, defer_large_content);
        Message msg(
            std::move(zmq_id),
            std::move(parts.header),
            std::move(parts.parent_header),
            std::move(parts.metadata),
            std::move(parts.content),
            std::move(parts.buffers)
        );
        if (parts.content_deferred)
        {
            msg.deferContent(std::move(parts.content_text));
        }
        return msg;
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
        const Authentication& auth,
        bool defer_large_content)
    {
        std::string topic = deserializeTopic(wire_msg);
        MessageParts parts = deserializeMessageBase(wire_msg, auth, defer_large_content);
        PubMessage msg(topic,
            std::move(parts.header),
            std::move(parts.parent_header),
            std::move(parts.metadata),
            std::move(parts.content),
            std::move(parts.buffers)
        );
        if (parts.content_deferred)
        {
            msg.deferContent(std::move(parts.content_text));
        }
        return msg;
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
        // native/test/adrastea/zmq_serializer_test.cpp): if the delimiter happened
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
