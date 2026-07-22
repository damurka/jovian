#ifndef DATASUITE_LOGGER_HPP
#define DATASUITE_LOGGER_HPP

#include <memory>

#include "datasuite.hpp"
#include "json.hpp"
#include "message.hpp"

namespace datasuite
{

    class DATASUITE_API logger
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

        virtual ~logger() = default;

        logger(const logger&) = delete;
        logger& operator=(const logger&) = delete;

        logger(logger&&) = delete;
        logger& operator=(logger&&) = delete;

        void log_received_message(const message& message, channel c) const;
        void log_sent_message(const message& message, channel c) const;
        void log_iopub_message(const pub_message& message) const;

        void log_message(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const;

    protected:

        logger() = default;

    private:

        virtual void log_received_message_impl(const message& message, channel c) const = 0;
        virtual void log_sent_message_impl(const message& message, channel c) const = 0;
        virtual void log_iopub_message_impl(const pub_message& message) const = 0;

        virtual void log_message_impl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const = 0;
    };

    DATASUITE_API
    std::unique_ptr<logger> make_console_logger(logger::level log_level,
            std::unique_ptr<logger> next_logger = nullptr);

    DATASUITE_API
    std::unique_ptr<logger> make_file_logger(logger::level log_level,
            const std::string& file_name,
            std::unique_ptr<logger> next_logger = nullptr);
}

#endif