#include <iostream>

#include "client_zmq_impl.hpp"
#include "../common/authentication.hpp"
#include "../common/zmq_serializer.hpp"

namespace adrastea
{
    namespace
    {
        constexpr std::size_t max_retry = 3;
        constexpr long heartbeat_timeout = std::chrono::milliseconds(20000).count();
    }

    ClientZmqImpl::ClientZmqImpl(zmq::context_t& context,
        const KernelConfiguration& config,
        json::error_handler_t eh)
        : p_auth(makeAuthentication(config.m_signatureScheme, config.m_key))
        , m_shellClient(context, config.m_transport, config.m_ip, config.m_shellPort)
        , m_controlClient(context, config.m_transport, config.m_ip, config.m_controlPort)
        , m_iopubClient(context, config, this)
        , m_heartbeatClient(context, config, max_retry, heartbeat_timeout)
        , p_messenger(context)
        , m_errorHandler(eh)
    {
    }

    // Has to be in the cpp because incomplete
    // types are used in unique_ptr in the header
    ClientZmqImpl::~ClientZmqImpl() = default;

    void ClientZmqImpl::sendOnShell(Message msg)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(msg), *p_auth, m_errorHandler);
        m_shellClient.sendMessage(wire_msg);
    }

    void ClientZmqImpl::sendOnControl(Message msg)
    {
        zmq::multipart_t wire_msg = ZmqSerializer::serialize(std::move(msg), *p_auth, m_errorHandler);
        m_controlClient.sendMessage(wire_msg);
    }

    std::optional<Message> ClientZmqImpl::receiveOnShell(bool blocking)
    {
        std::optional<zmq::multipart_t> wire_msg = m_shellClient.receiveMessage(blocking);

        if (wire_msg.has_value())
        {
            return deserialize(wire_msg.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    std::optional<Message> ClientZmqImpl::receiveOnControl(bool blocking)
    {
        std::optional<zmq::multipart_t> wire_msg = m_controlClient.receiveMessage(blocking);

        if (wire_msg.has_value())
        {
            return deserialize(wire_msg.value());
        }
        else
        {
            return std::nullopt;
        }
    }

    void ClientZmqImpl::registerShellListener(const listener& l)
    {
        m_shellListener = l;
    }

    void ClientZmqImpl::registerControlListener(const listener& l)
    {
        m_controlListener = l;
    }

    std::size_t ClientZmqImpl::iopubQueueSize() const
    {
        return m_iopubClient.iopubQueueSize();
    }

    std::optional<PubMessage> ClientZmqImpl::popIopubMessage()
    {
        return m_iopubClient.popIopubMessage();
    }

    void ClientZmqImpl::registerIopubListener(const iopub_listener& l)
    {
        m_iopubListener = l;
    }

    void ClientZmqImpl::registerKernelStatusListener(const kernel_status_listener& l)
    {
        m_heartbeatClient.registerKernelStatusListener(l);
    }

    void ClientZmqImpl::connect()
    {
        p_messenger.connect();
    }

    void ClientZmqImpl::stopChannels()
    {
        p_messenger.stopChannels();
    }

    void ClientZmqImpl::notifyShellListener(Message msg)
    {
        m_shellListener(std::move(msg));
    }

    void ClientZmqImpl::notifyControlListener(Message msg)
    {
        m_controlListener(std::move(msg));
    }

    void ClientZmqImpl::notifyIopubListener(PubMessage msg)
    {
        m_iopubListener(std::move(msg));
    }

    void ClientZmqImpl::notifyKernelDead(bool status)
    {
        m_heartbeatClient.notifyKernelDead(status);
    }

    void ClientZmqImpl::poll(long timeout)
    {
        zmq::multipart_t wire_msg;
        zmq::pollitem_t items[]
            = { { m_shellClient.getSocket(), 0, ZMQ_POLLIN, 0 }, { m_controlClient.getSocket(), 0, ZMQ_POLLIN, 0 } };

        while (true)
        {
            zmq::poll(&items[0], 2, std::chrono::milliseconds(timeout));
            try
            {
                if (items[0].revents & ZMQ_POLLIN)
                {
                    wire_msg.recv(m_shellClient.getSocket());
                    Message msg = deserialize(wire_msg);
                    notifyShellListener(std::move(msg));
                    return;
                }
                if (items[1].revents & ZMQ_POLLIN)
                {
                    wire_msg.recv(m_controlClient.getSocket());
                    Message msg = deserialize(wire_msg);
                    notifyControlListener(std::move(msg));
                    return;
                }
            }
            catch (std::exception& e)
            {
                std::cerr << e.what() << std::endl;
            }
        }
    }

    void ClientZmqImpl::waitForMessage()
    {
        std::optional<PubMessage> pending_message = popIopubMessage();

        if (pending_message.has_value())
        {
            notifyIopubListener(std::move(*pending_message));
        }
        else
        {
            poll(-1);
        }
    }

    void ClientZmqImpl::start()
    {
        startIopubThread();
        startHeartbeatThread();
    }

    void ClientZmqImpl::startIopubThread()
    {
        m_iopubThread = std::move(Thread(&ClientIopub::run, &m_iopubClient));
    }

    void ClientZmqImpl::startHeartbeatThread()
    {
        m_heartbeatThread = std::move(Thread(&ClientHeartbeat::run, &m_heartbeatClient));
    }

    Message ClientZmqImpl::deserialize(zmq::multipart_t& wire_msg) const
    {
        return ZmqSerializer::deserialize(wire_msg, *p_auth);
    }

    PubMessage ClientZmqImpl::deserializeIopub(zmq::multipart_t& wire_msg) const
    {
        return ZmqSerializer::deserializeIopub(wire_msg, *p_auth);
    }

}
