#ifndef ADRASTEA_SERVER_ZMQ_IMPL_HPP
#define ADRASTEA_SERVER_ZMQ_IMPL_HPP

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "zmq.hpp"
#include "zmq_addon.hpp"

#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/server.hpp"

#include "adrastea/adrastea.hpp"
#include "adrastea/thread.hpp"

#include "../common/authentication.hpp"
#include "publisher.hpp"
#include "heartbeat.hpp"
#include "trivial_messenger.hpp"

namespace adrastea
{
    class ServerZmqImpl
    {
    public:

        using listener = std::function<void(Message)>;
        using internal_listener = TrivialMessenger::listener;

        ServerZmqImpl(zmq::context_t& context,
            const configuration& initial_config,
            KernelConfiguration kernel_config,
            json::error_handler_t eh,
            internal_listener listener);

        ~ServerZmqImpl();

        // Control-channel service while code runs. The kernel executes code on
        // the same thread that polls its sockets, so without this an
        // interrupt_request sits unread in the control socket until the
        // execution it was meant to interrupt has finished. Between
        // beginExecution() and endExecution() a watcher thread reads the
        // control socket instead: interrupt_request goes to the interrupt
        // handler right away (on the watcher thread), anything else (e.g.
        // shutdown_request) is queued and delivered by pollChannels() once
        // the execution is over, exactly as before.
        void setInterruptHandler(listener handler);
        void beginExecution();
        void endExecution();

        void startPublisherThread();
        void startHeartbeatThread();
        void stopChannels();

        void setRequestStop(bool stop);
        bool isStopped() const;

        using message_channel = std::pair<Message, channel>;
        std::optional<message_channel> pollChannels(long timeout);

        ControlMessenger& getControlMessenger();

        void sendShell(Message message);
        void sendControl(Message message);
        std::optional<Message> sendStdin(Message message);
        void publish(PubMessage message, channel c);

        void abortQueue(const listener& l, long polling_interval);
        void updateConfig(KernelConfiguration& config) const;

        zmq::multipart_t serializeIopub(PubMessage&& msg);

    private:

        zmq::socket_t m_shell;
        zmq::socket_t m_controller;
        zmq::socket_t m_stdin;
        zmq::socket_t m_publisherPub;
        zmq::socket_t m_publisherController;
        zmq::socket_t m_heartbeatController;

        using authentication_ptr = std::unique_ptr<Authentication>;
        authentication_ptr p_auth;

        Publisher m_publisher;
        Heartbeat m_heartbeat;

        Thread m_iopubThread;
        Thread m_hbThread;

        TrivialMessenger m_messenger;

        json::error_handler_t m_errorHandler;

        bool m_requestStop;

        void watchControl();
        void pollControlOnce();
        bool isWatching();

        // Recursive: the interrupt handler, running on the watcher thread
        // with this held, replies through sendControl(), which takes it too.
        std::recursive_mutex m_controlMutex;
        // Serializes iopub publishing: the watcher thread's status/interrupt
        // messages and the execution thread's stream output share one PUB
        // socket, which must not be used from two threads at once.
        std::mutex m_publishMutex;
        std::deque<Message> m_deferredControl; // guarded by m_controlMutex
        listener m_interruptHandler;

        std::mutex m_watchMutex;
        std::condition_variable m_watchCv;
        bool m_watching = false;
        bool m_watchQuit = false;
        std::thread m_watchThread;
    };
}

#endif
