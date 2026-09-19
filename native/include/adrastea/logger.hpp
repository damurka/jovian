#ifndef ADRASTEA_LOGGER_HPP
#define ADRASTEA_LOGGER_HPP

#include <memory>

#include "adrastea.hpp"
#include "json.hpp"
#include "message.hpp"

namespace adrastea
{

    class ADRASTEA_API Logger
    {
    public:

        enum channel
        {
            shell,
            control,
            stdinput,
            heartbeat,
            CHANNEL_SIZE
        };

        enum level
        {
            msg_type,
            content,
            full
        };

        virtual ~Logger() = default;

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        void logReceivedMessage(const Message& message, channel c) const;
        void logSentMessage(const Message& message, channel c) const;
        void logIopubMessage(const PubMessage& message) const;

        void logMessage(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const;

    protected:

        Logger() = default;

    private:

        virtual void logReceivedMessageImpl(const Message& message, channel c) const = 0;
        virtual void logSentMessageImpl(const Message& message, channel c) const = 0;
        virtual void logIopubMessageImpl(const PubMessage& message) const = 0;

        virtual void logMessageImpl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const = 0;
    };

    ADRASTEA_API
    std::unique_ptr<Logger> makeConsoleLogger(Logger::level log_level,
            std::unique_ptr<Logger> next_logger = nullptr);

    ADRASTEA_API
    std::unique_ptr<Logger> makeFileLogger(Logger::level log_level,
            const std::string& file_name,
            std::unique_ptr<Logger> next_logger = nullptr);
}

#endif
