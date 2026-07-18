#include <chrono>
#include <cstddef>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"

#include "datasuite/guid.hpp"
#include "datasuite/message.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    message_base::message_base(
        nl::json header, nl::json parent_header, nl::json metadata, nl::json content, buffer_sequence buffers)
        : m_header(std::move(header))
        , m_parent_header(std::move(parent_header))
        , m_metadata(std::move(metadata))
        , m_content(std::move(content))
        , m_buffers(std::move(buffers))
    {
    }

    const nl::json& message_base::header() const
    {
        return m_header;
    }

    const nl::json& message_base::parent_header() const
    {
        return m_parent_header;
    }

    const nl::json& message_base::metadata() const
    {
        return m_metadata;
    }

    const nl::json& message_base::content() const
    {
        return m_content;
    }

    const buffer_sequence& message_base::buffers() const&
    {
        return m_buffers;
    }

    buffer_sequence&& message_base::buffers()&&
    {
        return std::move(m_buffers);
    }

    message::message(const guid_list& zmq_id,
        nl::json header,
        nl::json parent_header,
        nl::json metadata,
        nl::json content,
        buffer_sequence buffers)
        : message_base(std::move(header),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers))
        , m_zmq_id(zmq_id)
    {
    }

    message::message(const guid_list& zmq_id,
        message_base_data&& data)
        : message_base(std::move(data.m_header),
            std::move(data.m_parent_header),
            std::move(data.m_metadata),
            std::move(data.m_content),
            std::move(data.m_buffers))
        , m_zmq_id(zmq_id)
    {
    }

    auto message::identities() const -> const guid_list&
    {
        return m_zmq_id;
    }

    pub_message::pub_message(const std::string& topic,
        nl::json header,
        nl::json parent_header,
        nl::json metadata,
        nl::json content,
        buffer_sequence buffers)
        : message_base(std::move(header),
            std::move(parent_header),
            std::move(metadata),
            std::move(content),
            std::move(buffers))
        , m_topic(topic)
    {
    }

    pub_message::pub_message(const std::string& topic,
        message_base_data&& data)
        : message_base(std::move(data.m_header),
            std::move(data.m_parent_header),
            std::move(data.m_metadata),
            std::move(data.m_content),
            std::move(data.m_buffers))
        , m_topic(topic)
    {
    }

    const std::string& pub_message::topic() const
    {
        return m_topic;
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

    nl::json make_header(const std::string& msg_type,
        const std::string& user_name,
        const std::string& session_id)
    {
        nl::json header;
        header["msg_id"] = new_guid();
        header["username"] = user_name;
        header["session"] = session_id;
        header["date"] = iso8601_now();
        header["msg_type"] = msg_type;
        header["version"] = get_protocol_version();
        return header;
    }
}
