#include "zmq.hpp"
#include "zmq_addon.hpp"

#include <nlohmann/json.hpp>

#include "client_handshake_zmq.hpp"

#include "../common/authentication.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    /******************************
     * client_handshake_zmq_impl *
     ******************************/

    class client_handshake_zmq_impl
    {
    public:

        client_handshake_zmq_impl(zmq::context_t& context, const registration_configuration& config);

        std::string get_registration_port() const;

        kernel_configuration wait_for_configuration();

    private:

        zmq::context_t* p_context;
        std::string m_key;
        zmq::socket_t m_handshake;
        using authentication_ptr = std::unique_ptr<authentication>;
        authentication_ptr p_auth;
    };


    client_handshake_zmq_impl::client_handshake_zmq_impl
    (
        zmq::context_t& context,
        const registration_configuration& config
    )
        : p_context(&context)
        , m_key(config.m_key)
        , m_handshake(context, zmq::socket_type::router)
        , p_auth(make_authentication(config.m_signature_scheme, config.m_key))
    {
        init_socket(m_handshake, config.m_transport, config.m_registration_ip, config.m_registration_port);
    }

    std::string client_handshake_zmq_impl::get_registration_port() const
    {
        return get_socket_port(m_handshake);
    }

    kernel_configuration wait_for_configuration();

    kernel_configuration client_handshake_zmq_impl::wait_for_configuration()
    {
        zmq::multipart_t wire_msg;
        if (!wire_msg.recv(m_handshake))
        {
            throw std::runtime_error("Did not receive kernel configuration");
        }
        auto routing_ids = zmq_serializer::deserialize_zmq_id(wire_msg);
        // TODO: check signature
        wire_msg.pop(); // signature
        zmq::message_t content = wire_msg.pop();
        const char* buf = content.data<const char>();
        nl::json j = nl::json::parse(buf, buf + content.size());

        kernel_configuration config;
        config.m_key = m_key;
        // TODO: should we read and return kernel_id ?
        config.m_control_port = j["control_port"].get<std::string>();
        config.m_shell_port = j["shell_port"].get<std::string>();
        config.m_stdin_port = j["stdin_port"].get<std::string>();
        config.m_iopub_port = j["iopub_port"].get<std::string>();
        config.m_hb_port = j["hb_port"].get<std::string>();

        zmq::multipart_t wire_rep;
        std::string rep_buffer = "ACK";
        zmq::message_t rep_content(rep_buffer.c_str(), rep_buffer.size());
        auto auth = make_authentication("hmac-sha256", m_key);
        std::string sig = auth->sign(zmq_serializer::make_raw_buffer(rep_content));
        zmq::message_t signature(sig.begin(), sig.end());
        zmq_serializer::serialize_zmq_id(routing_ids, wire_rep);
        wire_rep.add(std::move(signature));
        wire_rep.add(std::move(rep_content));
        wire_rep.send(m_handshake);
        return config;
    }

    /*************************
     * client_handshake_zmq *
     *************************/

    client_handshake_zmq::client_handshake_zmq
    (
        context& context,
        const registration_configuration& config
    )
        : p_client_impl(new client_handshake_zmq_impl(context.get_wrapped_context<zmq::context_t>(), config))
    {
    }

    client_handshake_zmq::~client_handshake_zmq() = default;

    std::string client_handshake_zmq::get_registration_port() const
    {
        return p_client_impl->get_registration_port();
    }

    kernel_configuration client_handshake_zmq::wait_for_configuration()
    {
        return p_client_impl->wait_for_configuration();
    }
}
