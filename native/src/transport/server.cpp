#include <iostream>

#include "adrastea/server.hpp"

namespace adrastea
{
    ControlMessenger& Server::getControlMessenger()
    {
        return getControlMessengerImpl();
    }

    void Server::sendShell(Message message)
    {
        sendShellImpl(std::move(message));
    }

    void Server::sendControl(Message message)
    {
        sendControlImpl(std::move(message));
    }

    void Server::sendStdin(Message message)
    {
        sendStdinImpl(std::move(message));
    }

    void Server::publish(PubMessage message, channel c)
    {
        publishImpl(std::move(message), c);
    }

    void Server::start(PubMessage message)
    {
        startImpl(std::move(message));
    }

    void Server::abortQueue(const listener& l, long polling_interval)
    {
        abortQueueImpl(l, polling_interval);
    }

    void Server::stop()
    {
        stopImpl();
    }

    void Server::updateConfig(KernelConfiguration& config) const
    {
        updateConfigImpl(config);
    }

    void Server::registerShellListener(const listener& l)
    {
        m_shellListener = l;
    }

    void Server::registerControlListener(const listener& l)
    {
        m_controlListener = l;
    }

    void Server::registerStdinListener(const listener& l)
    {
        m_stdinListener = l;
    }

    void Server::registerInternalListener(const internal_listener& l)
    {
        m_internalListener = l;
    }

    void Server::notifyShellListener(Message msg)
    {
        if (m_shellListener) {
            m_shellListener(std::move(msg));
        }
        else {
            std::clog << "[Warning] Shell message received but no listener is registered!\n";
        }
    }

    void Server::notifyControlListener(Message msg)
    {
        if (m_controlListener) {
            m_controlListener(std::move(msg));
        }
    }

    void Server::notifyStdinListener(Message msg)
    {
        if (m_stdinListener) {
            m_stdinListener(std::move(msg));
        }
    }

    json Server::notifyInternalListener(json msg)
    {
        if (m_internalListener) {
            return m_internalListener(std::move(msg));
        }
        return json::object(); // Return empty JSON if no listener is attached
    }
}
