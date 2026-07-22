#ifndef DATASUITE_LOGGER_HPP
#define DATASUITE_LOGGER_HPP

#include <memory>

#include "datasuite.hpp"
#include "json.hpp"
#include "message.hpp"

namespace datasuite
{

    class DATASUITE_API Logger
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

        void log_received_message(const Message& message, channel c) const;
        void log_sent_message(const Message& message, channel c) const;
        void log_iopub_message(const PubMessage& message) const;

        void log_message(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const;

    protected:

        Logger() = default;

    private:

        virtual void log_received_message_impl(const Message& message, channel c) const = 0;
        virtual void log_sent_message_impl(const Message& message, channel c) const = 0;
        virtual void log_iopub_message_impl(const PubMessage& message) const = 0;

        virtual void log_message_impl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const = 0;
    };

    DATASUITE_API
    std::unique_ptr<Logger> make_console_logger(Logger::level log_level,
            std::unique_ptr<Logger> next_logger = nullptr);

    DATASUITE_API
    std::unique_ptr<Logger> make_file_logger(Logger::level log_level,
            const std::string& file_name,
            std::unique_ptr<Logger> next_logger = nullptr);
}

#endif
