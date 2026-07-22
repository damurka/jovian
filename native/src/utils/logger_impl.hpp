#include <array>
#include <mutex>
#include <string>

#include "datasuite/json.hpp"

#include "datasuite/logger.hpp"

namespace datasuite
{

    /*****************
     * logger_nolog *
     *****************/

    class logger_nolog : public logger
    {
    public:

        logger_nolog() = default;
        virtual ~logger_nolog() = default;

    private:

        void log_received_message_impl(const message& message, logger::channel c) const override;
        void log_sent_message_impl(const message& message, logger::channel c) const override;
        void log_iopub_message_impl(const pub_message& message) const override;

        void log_message_impl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const override;
    };

    /******************
     * logger_common *
     ******************/

    class logger_common : public logger
    {
    public:

        virtual ~logger_common();

    protected:

        using logger_ptr = std::unique_ptr<logger>;
        logger_common(logger::level l, logger_ptr next_logger = nullptr);

    private:

        void log_received_message_impl(const message& message, logger::channel c) const override;
        void log_sent_message_impl(const message& message, logger::channel c) const override;
        void log_iopub_message_impl(const pub_message& message) const override;

        void log_message_impl(const std::string& socket_info,
            const json& header,
            const json& parent_header,
            const json& metadata,
            const json& content) const override;

        virtual void log_json_message(const std::string& socket_info,
            const json& json_message) const = 0;

        logger_ptr p_nextLogger;
        logger::level m_level;
    };

    /*******************
     * logger_console *
     *******************/

    class logger_console : public logger_common
    {
    public:

        using logger_ptr = logger_common::logger_ptr;

        logger_console(logger::level l, logger_ptr next_logger = nullptr);
        virtual ~logger_console() = default;

    private:

        void log_json_message(const std::string& socket_info,
            const json& json_message) const override;

        mutable std::mutex m_mutex;
    };

    /****************
     * logger_file *
     ****************/

    class logger_file : public logger_common
    {
    public:

        using logger_ptr = logger_common::logger_ptr;

        logger_file(logger::level l,
            const std::string& file_name,
            logger_ptr next_logger = nullptr);
        virtual ~logger_file() = default;

    private:

        void log_json_message(const std::string& socket_info,
            const json& json_message) const override;

        std::string m_fileName;
        mutable std::mutex m_mutex;
    };
}
