#include "datasuite/logger.hpp"

#include "datasuite/json.hpp"

namespace datasuite
{
    void Logger::log_received_message(const Message& message, channel c) const
    {
        log_received_message_impl(message, c);
    }

    void Logger::log_sent_message(const Message& message, channel c) const
    {
        log_sent_message_impl(message, c);
    }

    void Logger::log_iopub_message(const PubMessage& message) const
    {
        log_iopub_message_impl(message);
    }

    void Logger::log_message(const std::string& socket_info,
        const json& header,
        const json& parent_header,
        const json& metadata,
        const json& content_json) const
    {
        log_message_impl(socket_info,
            header,
            parent_header,
            metadata,
            content_json);
    }
}
