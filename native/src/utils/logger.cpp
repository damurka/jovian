#include "datasuite/logger.hpp"

#include "datasuite/json.hpp"

namespace datasuite
{
    void Logger::logReceivedMessage(const Message& message, channel c) const
    {
        logReceivedMessageImpl(message, c);
    }

    void Logger::logSentMessage(const Message& message, channel c) const
    {
        logSentMessageImpl(message, c);
    }

    void Logger::logIopubMessage(const PubMessage& message) const
    {
        logIopubMessageImpl(message);
    }

    void Logger::logMessage(const std::string& socket_info,
        const json& header,
        const json& parent_header,
        const json& metadata,
        const json& content_json) const
    {
        logMessageImpl(socket_info,
            header,
            parent_header,
            metadata,
            content_json);
    }
}
