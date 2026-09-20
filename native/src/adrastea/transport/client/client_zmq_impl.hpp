#ifndef ADRASTEA_CLIENT_ZMQ_IMPL_HPP
#define ADRASTEA_CLIENT_ZMQ_IMPL_HPP

#include "zmq.hpp"

#include "adrastea/json.hpp"
#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/message.hpp"

#include "adrastea/thread.hpp"

#include "dealer_channel.hpp"
#include "client_iopub.hpp"
#include "client_heartbeat.hpp"
#include "client_messenger.hpp"

namespace adrastea
{
    class Authentication;

    class ClientZmqImpl
    {
    public:
        using listener = std::function<void(Message)>;
        using iopub_listener = std::function<void(PubMessage)>;
        using kernel_status_listener = std::function<void(bool)>;

        ClientZmqImpl(zmq::context_t& context,
            const KernelConfiguration& config,
            json::error_handler_t eh);

        ~ClientZmqImpl();

        ClientZmqImpl(const ClientZmqImpl&) = delete;
        ClientZmqImpl& operator=(const ClientZmqImpl&) = delete;

        ClientZmqImpl(ClientZmqImpl&&) = delete;
        ClientZmqImpl& operator=(ClientZmqImpl&&) = delete;

        // shell channel
        void sendOnShell(Message msg);
        std::optional<Message> receiveOnShell(bool blocking);
        void registerShellListener(const listener& l);

        // control channel
        void sendOnControl(Message msg);
        std::optional<Message> receiveOnControl(bool blocking);
        void registerControlListener(const listener& l);

        // stdin channel -- carries input_request (kernel -> here, when the
        // interpreter blocks on e.g. R's readline()/Python's input(), see
        // adrastea::blockingInputRequest()) and input_reply (here -> kernel,
        // unblocking it). Themisto's SessionRegistry polls receiveOnStdin()
        // the same way it already polls receiveOnShell(), relaying an
        // input_request over this session's WebSocket; sendOnStdin() is
        // what actually answers it once the browser replies.
        void sendOnStdin(Message msg);
        std::optional<Message> receiveOnStdin(bool blocking);
        void registerStdinListener(const listener& l);

        // iopub channel
        std::size_t iopubQueueSize() const;
        std::optional<PubMessage> popIopubMessage();
        void registerIopubListener(const iopub_listener& l);

        // heartbeat channel
        void registerKernelStatusListener(const kernel_status_listener& l);
        HeartbeatStatus heartbeatStatus() const;

        // client messenger
        void connect();
        void stopChannels();

        void waitForMessage();
        void start();

        Message deserialize(zmq::multipart_t& wire_msg) const;
        PubMessage deserializeIopub(zmq::multipart_t& wire_msg) const;

    private:

        void startIopubThread();
        void startHeartbeatThread();
        void poll(long timeout);

        void notifyShellListener(Message msg);
        void notifyControlListener(Message msg);
        void notifyStdinListener(Message msg);
        void notifyIopubListener(PubMessage msg);
        void notifyKernelDead(bool status);

        // See this class's constructor comment for why shell/control/stdin
        // all share this one identity.
        std::string m_identity;

        using authentication_ptr = std::unique_ptr<Authentication>;
        authentication_ptr p_auth;

        DealerChannel m_shellClient;
        DealerChannel m_controlClient;
        DealerChannel m_stdinClient;
        ClientIopub m_iopubClient;
        ClientHeartbeat m_heartbeatClient;

        ClientMessenger p_messenger;

        json::error_handler_t m_errorHandler;

        listener m_shellListener;
        listener m_controlListener;
        listener m_stdinListener;
        iopub_listener m_iopubListener;

        Thread m_iopubThread;
        Thread m_heartbeatThread;
    };
}

#endif
