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

    class DATASUITE_API Server
    {
    public:
        using listener = std::function<void(Message)>;
        using internal_listener = std::function<json(json)>;

        virtual ~Server() = default;

        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        Server(Server&&) = delete;
        Server& operator=(Server&&) = delete;

        ControlMessenger& get_control_messenger();

        void send_shell(Message message);
        void send_control(Message message);
        void send_stdin(Message message);
        void publish(PubMessage message, channel c);

        void start(PubMessage message);
        void abort_queue(const listener& l, long polling_interval);
        void stop();
        void update_config(KernelConfiguration& config) const;

        void register_shell_listener(const listener& l);
        void register_control_listener(const listener& l);
        void register_stdin_listener(const listener& l);
        void register_internal_listener(const internal_listener& l);

    protected:

        Server() = default;

        void notify_shell_listener(Message msg);
        void notify_control_listener(Message msg);
        void notify_stdin_listener(Message msg);
        json notify_internal_listener(json msg);

    private:

        virtual ControlMessenger& get_control_messenger_impl() = 0;

        virtual void send_shell_impl(Message message) = 0;
        virtual void send_control_impl(Message message) = 0;
        virtual void send_stdin_impl(Message message) = 0;
        virtual void publish_impl(PubMessage message, channel c) = 0;

        virtual void start_impl(PubMessage message) = 0;
        virtual void abort_queue_impl(const listener& l, long polling_interval) = 0;
        virtual void stop_impl() = 0;
        virtual void update_config_impl(KernelConfiguration& config) const = 0;

        listener m_shellListener;
        listener m_controlListener;
        listener m_stdinListener;
        internal_listener m_internalListener;
    };
}


#endif // DATASUITE_SERVER_HPP
