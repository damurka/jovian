#include <array>
#include <mutex>
#include <string>

#include "datasuite/json.hpp"

#include "datasuite/logger.hpp"

namespace datasuite
{

    /*****************
     * LoggerNolog *
     *****************/

    class LoggerNolog : public Logger
    {
    public:

        LoggerNolog() = default;
        virtual ~LoggerNolog() = default;

    private:

        void log_received_message_impl(const Message& message, Logger::channel c) const override;
        void log_sent_message_impl(const Message& message, Logger::channel c) const override;
        void log_iopub_message_impl(const PubMessage& message) const override;

        void log_message_impl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const override;
    };

    /******************
     * LoggerCommon *
     ******************/

    class LoggerCommon : public Logger
    {
    public:

        virtual ~LoggerCommon();

    protected:

        using logger_ptr = std::unique_ptr<Logger>;
        LoggerCommon(Logger::level l, logger_ptr next_logger = nullptr);

    private:

        void log_received_message_impl(const Message& message, Logger::channel c) const override;
        void log_sent_message_impl(const Message& message, Logger::channel c) const override;
        void log_iopub_message_impl(const PubMessage& message) const override;

        void log_message_impl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const override;

        virtual void log_json_message(const std::string& socket_info,
            const json& json_message) const = 0;

        logger_ptr p_nextLogger;
        Logger::level m_level;
    };

    /*******************
     * LoggerConsole *
     *******************/

    class LoggerConsole : public LoggerCommon
    {
    public:

        using logger_ptr = LoggerCommon::logger_ptr;

        LoggerConsole(Logger::level l, logger_ptr next_logger = nullptr);
        virtual ~LoggerConsole() = default;

    private:

        void log_json_message(const std::string& socket_info,
            const json& json_message) const override;

        mutable std::mutex m_mutex;
    };

    /****************
     * LoggerFile *
     ****************/

    class LoggerFile : public LoggerCommon
    {
    public:

        using logger_ptr = LoggerCommon::logger_ptr;

        LoggerFile(Logger::level l,
            const std::string& file_name,
            logger_ptr next_logger = nullptr);
        virtual ~LoggerFile() = default;

    private:

        void log_json_message(const std::string& socket_info,
            const json& json_message) const override;

        std::string m_fileName;
        mutable std::mutex m_mutex;
    };
}
