#include <iostream>

#include "datasuite/server.hpp"

namespace datasuite
{
    control_messenger& server::get_control_messenger()
    {
        return get_control_messenger_impl();
    }
    
    void server::send_shell(message message)
    {
        send_shell_impl(std::move(message));
    }

    void server::send_control(message message)
    {
        send_control_impl(std::move(message));
    }

    void server::send_stdin(message message)
    {
        send_stdin_impl(std::move(message));
    }

    void server::publish(pub_message message, channel c)
    {
        publish_impl(std::move(message), c);
    }

    void server::start(pub_message message)
    {
        start_impl(std::move(message));
    }

    void server::abort_queue(const listener& l, long polling_interval)
    {
        abort_queue_impl(l, polling_interval);
    }

    void server::stop()
    {
        stop_impl();
    }

    void server::update_config(KernelConfiguration& config) const
    {
        update_config_impl(config);
    }
    
    void server::register_shell_listener(const listener& l)
    {
        m_shellListener = l;
    }

    void server::register_control_listener(const listener& l)
    {
        m_controlListener = l;
    }

    void server::register_stdin_listener(const listener& l)
    {
        m_stdinListener = l;
    }

    void server::register_internal_listener(const internal_listener& l)
    {
        m_internalListener = l;
    }

    void server::notify_shell_listener(message msg)
    {
        if (m_shellListener) {
            m_shellListener(std::move(msg));
        }
        else {
            std::clog << "[Warning] Shell message received but no listener is registered!\n";
        }
    }

    void server::notify_control_listener(message msg)
    {
        if (m_controlListener) {
            m_controlListener(std::move(msg));
        }
    }

    void server::notify_stdin_listener(message msg)
    {
        if (m_stdinListener) {
            m_stdinListener(std::move(msg));
        }
    }

    nl::json server::notify_internal_listener(nl::json msg)
    {
        if (m_internalListener) {
            return m_internalListener(std::move(msg));
        }
        return nl::json::object(); // Return empty JSON if no listener is attached
    }
}
