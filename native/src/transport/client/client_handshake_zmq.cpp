#include "zmq.hpp"
#include "zmq_addon.hpp"

#include "datasuite/json.hpp"

#include "client_handshake_zmq.hpp"

#include "../common/authentication.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace datasuite
{
    /******************************
     * ClientHandshakeZmqImpl *
     ******************************/

    class ClientHandshakeZmqImpl
    {
    public:

        ClientHandshakeZmqImpl(zmq::context_t& context, const RegistrationConfiguration& config);

        std::string getRegistrationPort() const;

        KernelConfiguration waitForConfiguration();

    private:

        zmq::context_t* p_context;
        std::string m_key;
        zmq::socket_t m_handshake;
        using authentication_ptr = std::unique_ptr<Authentication>;
        authentication_ptr p_auth;
    };


    ClientHandshakeZmqImpl::ClientHandshakeZmqImpl
    (
        zmq::context_t& context,
        const RegistrationConfiguration& config
    )
        : p_context(&context)
        , m_key(config.m_key)
        , m_handshake(context, zmq::socket_type::router)
        , p_auth(makeAuthentication(config.m_signatureScheme, config.m_key))
    {
        initSocket(m_handshake, config.m_transport, config.m_registrationIp, config.m_registrationPort);
    }

    std::string ClientHandshakeZmqImpl::getRegistrationPort() const
    {
        return getSocketPort(m_handshake);
    }

    KernelConfiguration waitForConfiguration();

    KernelConfiguration ClientHandshakeZmqImpl::waitForConfiguration()
    {
        zmq::multipart_t wire_msg;
        if (!wire_msg.recv(m_handshake))
        {
            throw std::runtime_error("Did not receive kernel configuration");
        }
        auto routing_ids = ZmqSerializer::deserializeZmqId(wire_msg);
        // TODO: check signature
        wire_msg.pop(); // signature
        zmq::message_t content = wire_msg.pop();
        const char* buf = content.data<const char>();
        json j = json::parse(buf, buf + content.size());

        KernelConfiguration config;
        config.m_key = m_key;
        // TODO: should we read and return kernel_id ?
        config.m_controlPort = j["control_port"].get<std::string>();
        config.m_shellPort = j["shell_port"].get<std::string>();
        config.m_stdinPort = j["stdin_port"].get<std::string>();
        config.m_iopubPort = j["iopub_port"].get<std::string>();
        config.m_hbPort = j["hb_port"].get<std::string>();

        zmq::multipart_t wire_rep;
        std::string rep_buffer = "ACK";
        zmq::message_t rep_content(rep_buffer.c_str(), rep_buffer.size());
        auto auth = makeAuthentication("hmac-sha256", m_key);
        std::string sig = auth->sign(ZmqSerializer::makeRawBuffer(rep_content));
        zmq::message_t signature(sig.begin(), sig.end());
        ZmqSerializer::serializeZmqId(routing_ids, wire_rep);
        wire_rep.add(std::move(signature));
        wire_rep.add(std::move(rep_content));
        wire_rep.send(m_handshake);
        return config;
    }

    /*************************
     * ClientHandshakeZmq *
     *************************/

    ClientHandshakeZmq::ClientHandshakeZmq
    (
        Context& context,
        const RegistrationConfiguration& config
    )
        : p_clientImpl(new ClientHandshakeZmqImpl(context.getWrappedContext<zmq::context_t>(), config))
    {
    }

    ClientHandshakeZmq::~ClientHandshakeZmq() = default;

    std::string ClientHandshakeZmq::getRegistrationPort() const
    {
        return p_clientImpl->getRegistrationPort();
    }

    KernelConfiguration ClientHandshakeZmq::waitForConfiguration()
    {
        return p_clientImpl->waitForConfiguration();
    }
}
