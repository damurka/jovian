#include "datasuite/logger.hpp"

#include "datasuite/json.hpp"

namespace datasuite
{
    void logger::log_received_message(const message& message, channel c) const
    {
        log_received_message_impl(message, c);
    }

    void logger::log_sent_message(const message& message, channel c) const
    {
        log_sent_message_impl(message, c);
    }

    void logger::log_iopub_message(const pub_message& message) const
    {
        log_iopub_message_impl(message);
    }

    void logger::log_message(const std::string& socket_info,
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
