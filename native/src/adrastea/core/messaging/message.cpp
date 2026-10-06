#include <chrono>
#include <cstddef>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>

#include "adrastea/json.hpp"
#include "adrastea/guid.hpp"
#include "adrastea/message.hpp"

namespace adrastea
{
    MessageBase::MessageBase(
        json header,
        json parent_header,
        json metadata,
        json content,
        buffer_sequence buffers)
        : m_header(std::move(header))
        , m_parentHeader(std::move(parent_header))
        , m_metadata(std::move(metadata))
        , m_content(std::move(content))
        , m_buffers(std::move(buffers))
    {
    }

    void MessageBase::deferContent(std::string text)
    {
        m_contentText = std::move(text);
        m_hasContentText = true;
        m_contentDeferred = true;
        m_content = json();
    }

    void MessageBase::parseDeferredContent() const
    {
        m_contentDeferred = false;
        // as the serializer parses a frame: comments ignored, and a text that is not JSON is an error object
        m_content = json::parse(m_contentText, nullptr, false, true);
        if (m_content.is_discarded())
        {
            m_content = json::object();
            m_content["error"] = "JSON parse error: the message's content is not JSON";
        }
    }

    Message::Message(
        const guid_list& zmq_id,
        json header,
        json parent_header,
        json metadata,
        json content,
        buffer_sequence buffers)
        : MessageBase(std::move(header),
                        std::move(parent_header),
                        std::move(metadata),
                        std::move(content),
                        std::move(buffers))
        , m_zmqId(zmq_id)
    {
    }

    PubMessage::PubMessage(const std::string& topic,
        json header,
        json parent_header,
        json metadata,
        json content,
        buffer_sequence buffers)
        : MessageBase(std::move(header),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers))
        , m_topic(topic)
    {
    }

    std::string iso8601Now()
    {
        std::ostringstream ss;

        // now
        auto now = std::chrono::system_clock::now();

        // down to seconds
        auto itt = std::chrono::system_clock::to_time_t(now);
        ss << std::put_time(std::gmtime(&itt), "%FT%T");

        // down to microseconds
        auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch());
        auto fractionals = micros.count() % 1000000;
        ss << "." << fractionals << "Z";

        return ss.str();
    }

    std::string_view getProtocolVersion()
    {
        return adrastea::version::kernel_protocol_version;
    }

    json makeHeader(const std::string& msg_type,
        const std::string& user_name,
        const std::string& session_id)
    {
        json header;
        header["msg_id"] = newGuid();
        header["username"] = user_name;
        header["session"] = session_id;
        header["date"] = iso8601Now();
        header["msg_type"] = msg_type;
        header["version"] = getProtocolVersion();
        return header;
    }
}
