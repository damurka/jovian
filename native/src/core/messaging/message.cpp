#include <chrono>
#include <cstddef>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>

#include "datasuite/json.hpp"
#include "datasuite/guid.hpp"
#include "datasuite/message.hpp"

namespace datasuite
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

    std::string iso8601_now()
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

    std::string_view get_protocol_version()
    {
        return datasuite::version::kernel_protocol_version;
    }

    json make_header(const std::string& msg_type,
        const std::string& user_name,
        const std::string& session_id)
    {
        json header;
        header["msg_id"] = new_guid();
        header["username"] = user_name;
        header["session"] = session_id;
        header["date"] = iso8601_now();
        header["msg_type"] = msg_type;
        header["version"] = get_protocol_version();
        return header;
    }
}
