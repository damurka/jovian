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

        std::string get_registration_port() const;

        KernelConfiguration wait_for_configuration();

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
        , p_auth(make_authentication(config.m_signatureScheme, config.m_key))
    {
        init_socket(m_handshake, config.m_transport, config.m_registrationIp, config.m_registrationPort);
    }

    std::string ClientHandshakeZmqImpl::get_registration_port() const
    {
        return get_socket_port(m_handshake);
    }

    KernelConfiguration wait_for_configuration();

    KernelConfiguration ClientHandshakeZmqImpl::wait_for_configuration()
    {
        zmq::multipart_t wire_msg;
        if (!wire_msg.recv(m_handshake))
        {
            throw std::runtime_error("Did not receive kernel configuration");
        }
        auto routing_ids = ZmqSerializer::deserialize_zmq_id(wire_msg);
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
        auto auth = make_authentication("hmac-sha256", m_key);
        std::string sig = auth->sign(ZmqSerializer::make_raw_buffer(rep_content));
        zmq::message_t signature(sig.begin(), sig.end());
        ZmqSerializer::serialize_zmq_id(routing_ids, wire_rep);
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
        : p_clientImpl(new ClientHandshakeZmqImpl(context.get_wrapped_context<zmq::context_t>(), config))
    {
    }

    ClientHandshakeZmq::~ClientHandshakeZmq() = default;

    std::string ClientHandshakeZmq::get_registration_port() const
    {
        return p_clientImpl->get_registration_port();
    }

    KernelConfiguration ClientHandshakeZmq::wait_for_configuration()
    {
        return p_clientImpl->wait_for_configuration();
    }
}
