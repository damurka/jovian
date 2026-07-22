#include <iostream>

#include "datasuite/server.hpp"

namespace datasuite
{
    ControlMessenger& Server::get_control_messenger()
    {
        return get_control_messenger_impl();
    }

    void Server::send_shell(Message message)
    {
        send_shell_impl(std::move(message));
    }

    void Server::send_control(Message message)
    {
        send_control_impl(std::move(message));
    }

    void Server::send_stdin(Message message)
    {
        send_stdin_impl(std::move(message));
    }

    void Server::publish(PubMessage message, channel c)
    {
        publish_impl(std::move(message), c);
    }

    void Server::start(PubMessage message)
    {
        start_impl(std::move(message));
    }

    void Server::abort_queue(const listener& l, long polling_interval)
    {
        abort_queue_impl(l, polling_interval);
    }

    void Server::stop()
    {
        stop_impl();
    }

    void Server::update_config(KernelConfiguration& config) const
    {
        update_config_impl(config);
    }

    void Server::register_shell_listener(const listener& l)
    {
        m_shellListener = l;
    }

    void Server::register_control_listener(const listener& l)
    {
        m_controlListener = l;
    }

    void Server::register_stdin_listener(const listener& l)
    {
        m_stdinListener = l;
    }

    void Server::register_internal_listener(const internal_listener& l)
    {
        m_internalListener = l;
    }

    void Server::notify_shell_listener(Message msg)
    {
        if (m_shellListener) {
            m_shellListener(std::move(msg));
        }
        else {
            std::clog << "[Warning] Shell message received but no listener is registered!\n";
        }
    }

    void Server::notify_control_listener(Message msg)
    {
        if (m_controlListener) {
            m_controlListener(std::move(msg));
        }
    }

    void Server::notify_stdin_listener(Message msg)
    {
        if (m_stdinListener) {
            m_stdinListener(std::move(msg));
        }
    }

    json Server::notify_internal_listener(json msg)
    {
        if (m_internalListener) {
            return m_internalListener(std::move(msg));
        }
        return json::object(); // Return empty JSON if no listener is attached
    }
}
