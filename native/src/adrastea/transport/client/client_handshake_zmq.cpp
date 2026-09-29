#include <chrono>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include "zmq.hpp"
#include "zmq_addon.hpp"

#include "adrastea/json.hpp"

#include "client_handshake_zmq.hpp"

#include "../common/authentication.hpp"
#include "../common/middleware_impl.hpp"
#include "../common/zmq_serializer.hpp"

namespace adrastea
{
    /******************************
     * ClientHandshakeZmqImpl *
     ******************************/

    namespace
    {
        // Short, so waitForConfiguration() can check its shouldAbort
        // predicate (e.g. "has the kernel process already died?") frequently
        // rather than committing to one long blocking recv.
        constexpr int kPollIntervalMs = 250;

        // Generous on purpose, not tuned: this bounds the *overall* wait for
        // a newly spawned kernel process to finish its own startup (for
        // elara: process launch, Rf_initEmbeddedR, loading hera) and then
        // dial back in -- unlike sendConnectionInfo()'s 5s ack-wait
        // (handshaking.cpp), the kernel hasn't even connected yet when this
        // clock starts, so it has to cover real interpreter startup time, not
        // just network round-trip. Only matters when no shouldAbort predicate
        // is given, or the process is alive but never registers for some
        // other reason -- a dead process is caught almost immediately via
        // the poll loop below instead of waiting this out.
        constexpr int kRegistrationTimeoutMs = 60000;
    }

    class ClientHandshakeZmqImpl
    {
    public:

        ClientHandshakeZmqImpl(zmq::context_t& context, const RegistrationConfiguration& config);

        std::string getRegistrationPort() const;

        KernelConfiguration waitForConfiguration(const std::function<bool()>& shouldAbort);

    private:

        // The configuration from a JEP 66 handshake_request (answered here
        // with a signed handshake_reply), or nullopt when `wire_msg` is not
        // one -- it is then left untouched for the short form.
        std::optional<KernelConfiguration> readJep66Handshake(zmq::multipart_t& wire_msg);

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
        // Without this, a kernel process that fails to start or crashes
        // before registering (e.g. elara.exe exiting immediately because R
        // couldn't be loaded -- see native/src/elara/r/r_dynlib.cpp) hangs
        // waitForConfiguration() below forever: confirmed directly, a real
        // createSession() call with no R_HOME configured never returned,
        // leaving a live but permanently-stuck themisto.exe behind. A plain
        // zmq recv (this socket's default) blocks indefinitely with no
        // timeout at all -- this was a documented, flagged-but-unfixed
        // limitation (see session_registry.cpp's createSessionWithId())
        // until now. Short (kPollIntervalMs), not the full timeout: see
        // waitForConfiguration()'s poll loop below for why.
        m_handshake.set(zmq::sockopt::rcvtimeo, kPollIntervalMs);
    }

    std::string ClientHandshakeZmqImpl::getRegistrationPort() const
    {
        return getSocketPort(m_handshake);
    }

    std::optional<KernelConfiguration> ClientHandshakeZmqImpl::readJep66Handshake(zmq::multipart_t& wire_msg)
    {
        // Routing frames, then <IDS|MSG>, then signature, header, parent
        // header, metadata, content: at least five frames after the
        // delimiter. The short form has two.
        std::size_t delimiterAt = wire_msg.size();
        for (std::size_t i = 0; i < wire_msg.size(); ++i)
        {
            const zmq::message_t& frame = wire_msg[i];
            if (std::string(frame.data<const char>(), frame.size()) == "<IDS|MSG>")
            {
                delimiterAt = i;
                break;
            }
        }
        if (delimiterAt == wire_msg.size() || wire_msg.size() - delimiterAt - 1 < 5)
        {
            return std::nullopt;
        }

        zmq::multipart_t copy = wire_msg.clone();
        Message request;
        try
        {
            request = ZmqSerializer::deserialize(copy, *p_auth); // checks the signature
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(std::string("Rejected a kernel's handshake: ") + e.what());
        }
        if (request.header().value("msg_type", "") != "handshake_request")
        {
            return std::nullopt;
        }

        const json& content = request.content();
        auto port = [&](const char* name) {
            const json& value = content.at(name);
            return value.is_string() ? value.get<std::string>() : std::to_string(value.get<long long>());
        };
        KernelConfiguration config;
        config.m_key = m_key;
        config.m_controlPort = port("control_port");
        config.m_shellPort = port("shell_port");
        config.m_stdinPort = port("stdin_port");
        config.m_iopubPort = port("iopub_port");
        config.m_hbPort = port("hb_port");

        // Back through the same routing frames (for a REQ peer they include
        // its empty envelope frame, which it strips on receipt).
        Message reply(request.identities(),
            makeHeader("handshake_reply", "themisto", request.header().value("session", "")),
            request.header(),
            json::object(),
            json{ { "status", "ok" } },
            buffer_sequence());
        zmq::multipart_t wire_reply = ZmqSerializer::serialize(std::move(reply), *p_auth, json::error_handler_t::replace);
        wire_reply.send(m_handshake);
        wire_msg.clear();
        return config;
    }

    KernelConfiguration ClientHandshakeZmqImpl::waitForConfiguration(const std::function<bool()>& shouldAbort)
    {
        zmq::multipart_t wire_msg;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kRegistrationTimeoutMs);
        while (!wire_msg.recv(m_handshake))
        {
            // A dead kernel process can never register -- confirmed
            // directly, e.g. elara.exe exits in well under 100ms when R
            // can't be loaded, but a single long-timeout recv used to make
            // callers wait out the *entire* timeout regardless (a real,
            // observed 60s wait for what was actually an instant failure).
            // Checked every kPollIntervalMs rather than assumed dead the
            // first time this loop runs, so a process that's simply slow to
            // connect isn't penalized.
            if (shouldAbort && shouldAbort())
            {
                throw std::runtime_error(
                    "Kernel process exited before it could register -- check its stderr output "
                    "for the actual error.");
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                throw std::runtime_error(
                    "Did not receive kernel configuration within " +
                    std::to_string(kRegistrationTimeoutMs / 1000) +
                    "s -- the kernel process is still running but never registered. Check its "
                    "stderr output for what it's doing.");
            }
        }
        // Two forms. A kernel following JEP 66 (the Jupyter handshake, e.g.
        // Posit's Ark) sends a complete signed Jupyter message --
        // handshake_request, ports as numbers -- from a REQ socket and waits
        // for a signed handshake_reply. Adrastea's own kernels send the
        // shorter form below: a signature and the ports as strings.
        if (auto jep66 = readJep66Handshake(wire_msg))
        {
            return *jep66;
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

    KernelConfiguration ClientHandshakeZmq::waitForConfiguration(const std::function<bool()>& shouldAbort)
    {
        return p_clientImpl->waitForConfiguration(shouldAbort);
    }
}
