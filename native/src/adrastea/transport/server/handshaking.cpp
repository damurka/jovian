#include "zmq_addon.hpp"
#include "adrastea/json.hpp"

#include "handshaking.hpp"
#include "adrastea/middleware.hpp"

#include "../common/zmq_serializer.hpp"

namespace adrastea
{
    KernelConfiguration getKernelConfiguration(const configuration& config)
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

    void sendConnectionInfo(
        zmq::context_t& context,
        const RegistrationConfiguration& regis_config,
        const KernelConfiguration& kernel_config,
        const Authentication& auth,
        json::error_handler_t error_handler)
    {
        std::string end_point = getEndPoint(
            regis_config.m_transport,
            regis_config.m_registrationIp,
            regis_config.m_registrationPort);
        zmq::socket_t socket(context, zmq::socket_type::dealer);
        socket.set(zmq::sockopt::linger, getSocketLinger());
        socket.set(zmq::sockopt::rcvtimeo, 5000);
        socket.connect(end_point);

        zmq::multipart_t wire_msg;
        ZmqSerializer::serializeZmqId({}, wire_msg);

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
        std::string sig = auth.sign(ZmqSerializer::makeRawBuffer(content));
        zmq::message_t signature(sig.begin(), sig.end());
        wire_msg.add(std::move(signature));
        wire_msg.add(std::move(content));

        wire_msg.send(socket);

        zmq::multipart_t rep;
        rep.recv(socket);

        ZmqSerializer::deserializeZmqId(rep);
        zmq::message_t rep_sig = rep.pop();
        zmq::message_t rep_content = rep.pop();
        if (!auth.verify(ZmqSerializer::makeRawBuffer(rep_sig),
            ZmqSerializer::makeRawBuffer(rep_content)))
        {
            throw std::runtime_error("ERROR: Signatures don't match");
        }
    }
}
