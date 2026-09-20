#include <array>
#include <mutex>
#include <string>

#include "adrastea/json.hpp"

#include "adrastea/logger.hpp"

namespace adrastea
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

        void logReceivedMessageImpl(const Message& message, Logger::channel c) const override;
        void logSentMessageImpl(const Message& message, Logger::channel c) const override;
        void logIopubMessageImpl(const PubMessage& message) const override;

        void logMessageImpl(const std::string& socket_info,
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

        void logReceivedMessageImpl(const Message& message, Logger::channel c) const override;
        void logSentMessageImpl(const Message& message, Logger::channel c) const override;
        void logIopubMessageImpl(const PubMessage& message) const override;

        void logMessageImpl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const override;

        virtual void logJsonMessage(const std::string& socket_info,
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

        void logJsonMessage(const std::string& socket_info,
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

        void logJsonMessage(const std::string& socket_info,
            const json& json_message) const override;

        std::string m_fileName;
        mutable std::mutex m_mutex;
    };
}
