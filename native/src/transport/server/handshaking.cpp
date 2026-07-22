#include "zmq_addon.hpp"
#include "datasuite/json.hpp"

#include "handshaking.hpp"
#include "datasuite/middleware.hpp"

#include "../common/zmq_serializer.hpp"

namespace datasuite
{
    KernelConfiguration get_kernel_configuration(const configuration& config)
    {
        return std::visit([](const auto& conf)
            {
                if constexpr (std::is_same_v<std::decay_t<decltype(conf)>, KernelConfiguration>)
                {
                    return conf;
                }
                else
                {
                    KernelConfiguration res;
                    res.m_transport = conf.m_transport;
                    res.m_ip = conf.m_ip;
                    res.m_signatureScheme = conf.m_signatureScheme;
                    res.m_key = conf.m_key;
                    return res;
                }
            }, config);
    }

    void send_connection_info(
        zmq::context_t& context,
        const RegistrationConfiguration& regis_config,
        const KernelConfiguration& kernel_config,
        const Authentication& auth,
        json::error_handler_t error_handler)
    {
        std::string end_point = get_end_point(
            regis_config.m_transport,
            regis_config.m_registrationIp,
            regis_config.m_registrationPort);
        zmq::socket_t socket(context, zmq::socket_type::dealer);
        socket.set(zmq::sockopt::linger, get_socket_linger());
        socket.set(zmq::sockopt::rcvtimeo, 5000);
        socket.connect(end_point);

        zmq::multipart_t wire_msg;
        ZmqSerializer::serialize_zmq_id({}, wire_msg);

        json msg = {
            { "kernel_id", regis_config.m_kernelId },
            { "control_port", kernel_config.m_controlPort },
            { "shell_port", kernel_config.m_shellPort },
            { "stdin_port", kernel_config.m_stdinPort },
            { "iopub_port", kernel_config.m_iopubPort },
            { "hb_port", kernel_config.m_hbPort }
        };
        std::string buffer = msg.dump(-1, ' ', false, error_handler);
        zmq::message_t content(buffer.c_str(), buffer.size());
        std::string sig = auth.sign(ZmqSerializer::make_raw_buffer(content));
        zmq::message_t signature(sig.begin(), sig.end());
        wire_msg.add(std::move(signature));
        wire_msg.add(std::move(content));

        wire_msg.send(socket);

        zmq::multipart_t rep;
        rep.recv(socket);

        ZmqSerializer::deserialize_zmq_id(rep);
        zmq::message_t rep_sig = rep.pop();
        zmq::message_t rep_content = rep.pop();
        if (!auth.verify(ZmqSerializer::make_raw_buffer(rep_sig),
            ZmqSerializer::make_raw_buffer(rep_content)))
        {
            throw std::runtime_error("ERROR: Signatures don't match");
        }
    }
}
