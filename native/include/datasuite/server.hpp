#ifndef DATASUITE_SERVER_HPP
#define DATASUITE_SERVER_HPP

#include <functional>

#include "control_messenger.hpp"
#include "datasuite.hpp"
#include "kernel_configuration.hpp"
#include "message.hpp"

namespace datasuite
{
    enum class channel
    {
        SHELL,
        CONTROL
    };

    class DATASUITE_API server
    {
    public:
        using listener = std::function<void(message)>;
        using internal_listener = std::function<nl::json(nl::json)>;

        virtual ~server() = default;

        server(const server&) = delete;
        server& operator=(const server&) = delete;

        server(server&&) = delete;
        server& operator=(server&&) = delete;

        control_messenger& get_control_messenger();

        void send_shell(message message);
        void send_control(message message);
        void send_stdin(message message);
        void publish(pub_message message, channel c);

        void start(pub_message message);
        void abort_queue(const listener& l, long polling_interval);
        void stop();
        void update_config(kernel_configuration& config) const;

        void register_shell_listener(const listener& l);
        void register_control_listener(const listener& l);
        void register_stdin_listener(const listener& l);
        void register_internal_listener(const internal_listener& l);

    protected:

        server() = default;

        void notify_shell_listener(message msg);
        void notify_control_listener(message msg);
        void notify_stdin_listener(message msg);
        nl::json notify_internal_listener(nl::json msg);

    private:

        virtual control_messenger& get_control_messenger_impl() = 0;

        virtual void send_shell_impl(message message) = 0;
        virtual void send_control_impl(message message) = 0;
        virtual void send_stdin_impl(message message) = 0;
        virtual void publish_impl(pub_message message, channel c) = 0;

        virtual void start_impl(pub_message message) = 0;
        virtual void abort_queue_impl(const listener& l, long polling_interval) = 0;
        virtual void stop_impl() = 0;
        virtual void update_config_impl(kernel_configuration& config) const = 0;

        listener m_shell_listener;
        listener m_control_listener;
        listener m_stdin_listener;
        internal_listener m_internal_listener;
    };
}


#endif // DATASUITE_SERVER_HPP