// Coverage for ClientZmqImpl/ClientZmq (transport/client/client_zmq_impl.cpp,
// client_zmq.cpp) plus everything they compose (DealerChannel, ClientIopub's
// message-received path, ClientMessenger's connect/stopChannels). Before
// this, the only thing exercising this stack was SessionRegistryTest's real
// elara.exe kernel -- which only ever drives sendOnShell/
// receiveOnShell(false) via SessionRegistry::pollLoop(), never poll(),
// waitForMessage(), the shell/control *listener* callbacks, or a real iopub
// publish. This file stands in a hand-rolled "fake kernel" (bare ROUTER/PUB
// sockets, no R, no spawned process) to drive those paths directly.
#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/message.hpp"
#include "adrastea/transport/client/client_zmq.hpp"
#include "adrastea/transport/common/authentication.hpp"
#include "adrastea/transport/common/middleware_impl.hpp"
#include "adrastea/transport/common/zmq_serializer.hpp"

using namespace adrastea;

namespace
{
    constexpr const char* kKey = "fake-kernel-key";

    // Polls `predicate` until it returns true or `timeoutMs` elapses -- same
    // shape as the helper in session_registry_test.cpp, duplicated rather
    // than shared since these are independent, single-file test binaries.
    bool waitFor(const std::function<bool()>& predicate, int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return predicate();
    }

    // Stands in for the elara kernel process ClientZmq normally talks
    // to: bare ROUTER sockets for shell/control (mirrors server_zmq_impl.cpp),
    // a PUB socket for iopub, and an unattended REP socket for heartbeat
    // (never answered -- ClientHeartbeat's hardcoded 20s timeout means the
    // tests below finish via stopChannels()'s controller signal long before
    // a single heartbeat round trip would time out, so nothing needs to
    // listen on it; see client_heartbeat_test.cpp for heartbeat's own
    // dead-kernel-detection behavior, tested in isolation with short
    // timeouts instead of piggybacking on this hardcoded 20s constant).
    class FakeKernel
    {
    public:
        explicit FakeKernel(zmq::context_t& ctx)
            : m_shellRouter(ctx, zmq::socket_type::router)
            , m_controlRouter(ctx, zmq::socket_type::router)
            , m_stdinRouter(ctx, zmq::socket_type::router)
            , m_iopubPub(ctx, zmq::socket_type::pub)
            , m_hbRep(ctx, zmq::socket_type::rep)
        {
            m_shellRouter.bind("tcp://127.0.0.1:0");
            m_controlRouter.bind("tcp://127.0.0.1:0");
            m_stdinRouter.bind("tcp://127.0.0.1:0");
            m_iopubPub.bind("tcp://127.0.0.1:0");
            m_hbRep.bind("tcp://127.0.0.1:0");
        }

        std::string shellPort() const { return getSocketPort(m_shellRouter); }
        std::string controlPort() const { return getSocketPort(m_controlRouter); }
        // ClientZmqImpl unconditionally connects a stdin DealerChannel now
        // (see this file's own header comment update below) -- even tests
        // that never exercise sendOnStdin/receiveOnStdin need a real ROUTER
        // bound here, or that connect() throws ("Invalid argument" from an
        // empty-port endpoint) before any test body runs.
        std::string stdinPort() const { return getSocketPort(m_stdinRouter); }
        std::string iopubPort() const { return getSocketPort(m_iopubPub); }
        std::string hbPort() const { return getSocketPort(m_hbRep); }

        Message recvShellRequest(const Authentication& auth)
        {
            zmq::multipart_t wire;
            wire.recv(m_shellRouter);
            return ZmqSerializer::deserialize(wire, auth);
        }

        void sendShellReply(Message&& reply, const Authentication& auth)
        {
            zmq::multipart_t wire = ZmqSerializer::serialize(std::move(reply), auth);
            wire.send(m_shellRouter);
        }

        Message recvControlRequest(const Authentication& auth)
        {
            zmq::multipart_t wire;
            wire.recv(m_controlRouter);
            return ZmqSerializer::deserialize(wire, auth);
        }

        void sendControlReply(Message&& reply, const Authentication& auth)
        {
            zmq::multipart_t wire = ZmqSerializer::serialize(std::move(reply), auth);
            wire.send(m_controlRouter);
        }

        void publishIopub(PubMessage&& msg, const Authentication& auth)
        {
            zmq::multipart_t wire = ZmqSerializer::serializeIopub(std::move(msg), auth);
            wire.send(m_iopubPub);
        }

        // Mirrors ServerZmqImpl::sendStdin() -- a ROUTER send addressed by
        // whatever identity list `msg` itself carries (msg.identities()),
        // exactly how KernelCore::sendStdin() addresses a real input_request
        // using the identity it captured from a *different* channel's
        // (shell's) incoming request.
        void sendStdinRequest(Message&& msg, const Authentication& auth)
        {
            zmq::multipart_t wire = ZmqSerializer::serialize(std::move(msg), auth);
            wire.send(m_stdinRouter);
        }

    private:
        zmq::socket_t m_shellRouter;
        zmq::socket_t m_controlRouter;
        zmq::socket_t m_stdinRouter;
        zmq::socket_t m_iopubPub;
        zmq::socket_t m_hbRep;
    };

    KernelConfiguration makeConfig(const FakeKernel& kernel)
    {
        KernelConfiguration config;
        config.m_transport = "tcp";
        config.m_ip = "127.0.0.1";
        config.m_signatureScheme = "hmac-sha256";
        config.m_key = kKey;
        config.m_shellPort = kernel.shellPort();
        config.m_controlPort = kernel.controlPort();
        config.m_stdinPort = kernel.stdinPort();
        config.m_iopubPort = kernel.iopubPort();
        config.m_hbPort = kernel.hbPort();
        return config;
    }

    Message makeRequest(const std::string& msgType, const json& content)
    {
        json header = makeHeader(msgType, "client_user", "session-1");
        return Message({}, header, json::object(), json::object(), content, buffer_sequence());
    }

    Message makeReplyTo(const Message& request, const std::string& msgType, const json& content)
    {
        json header = makeHeader(msgType, "kernel", "session-1");
        return Message(request.identities(), header, request.header(), json::object(), content, buffer_sequence());
    }

    // RAII around connect()+start()+stopChannels(): every test below needs
    // this exact bracketing -- ClientIopub::run()/ClientHeartbeat::run() are
    // infinite loops that only exit on stopChannels()'s controller signal,
    // and adrastea::Thread (their storage type) joins in its own destructor,
    // so skipping stopChannels() before a client goes out of scope hangs the
    // test rather than failing it.
    struct StartedClient
    {
        std::unique_ptr<Context> context = makeZmqContext();
        std::unique_ptr<ClientZmq> client;

        explicit StartedClient(const KernelConfiguration& config)
            : client(makeClientZmq(*context, config))
        {
            client->connect();
            client->start();
        }

        ~StartedClient() { client->stopChannels(); }
    };
}

TEST(ClientZmqTest, SendOnShellAndReceiveOnShellRoundTrips)
{
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);
    auto kernelAuth = makeAuthentication("hmac-sha256", kKey);

    StartedClient sc(makeConfig(fakeKernel));

    sc.client->sendOnShell(makeRequest("execute_request", { { "code", "1 + 1" } }));

    Message request = fakeKernel.recvShellRequest(*kernelAuth);
    EXPECT_EQ(request.header().at("msg_type").get<std::string>(), "execute_request");
    fakeKernel.sendShellReply(makeReplyTo(request, "execute_reply", { { "status", "ok" } }), *kernelAuth);

    auto result = sc.client->receiveOnShell(true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().at("msg_type").get<std::string>(), "execute_reply");
    EXPECT_EQ(result->content().at("status").get<std::string>(), "ok");
}

TEST(ClientZmqTest, StdinInputRequestReachesTheClientUsingTheIdentityCapturedFromShell)
{
    // Regression test for a real, confirmed-end-to-end bug: KernelCore::
    // sendStdin() (kernel_core.cpp) addresses its ROUTER send on the STDIN
    // socket using the ZMQ routing identity list it captured from whichever
    // execute_request arrived on the SHELL socket (RequestContext::id()) --
    // not a fresh receive on the stdin socket itself. This only reaches the
    // real client if that same client presents an IDENTICAL identity to the
    // stdin ROUTER too. Before ClientZmqImpl gave its shell/control/stdin
    // DealerChannels a shared, explicit ZMQ_ROUTING_ID (see its constructor
    // comment), each independently got its own random one, so this identity
    // reuse silently matched no connected peer -- the send was dropped, and
    // the kernel's real, untimed ZMQ recv underneath (ServerZmqImpl::
    // sendStdin()) blocked forever with no way to ever be answered: exactly
    // the "input()/readline() hangs forever" bug this whole feature exists
    // to fix, just never previously caught because nothing exercised a
    // *cross-channel* identity reuse this way.
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);
    auto kernelAuth = makeAuthentication("hmac-sha256", kKey);

    StartedClient sc(makeConfig(fakeKernel));

    sc.client->sendOnShell(makeRequest("execute_request", { { "code", "input('x?')" } }));
    Message request = fakeKernel.recvShellRequest(*kernelAuth);

    // Exactly what RequestContext::id() would capture from this request on
    // the real kernel side, and later reuse to address the stdin send.
    auto capturedIdentity = request.identities();
    ASSERT_FALSE(capturedIdentity.empty());

    Message inputRequest(
        capturedIdentity,
        makeHeader("input_request", "kernel", "session-1"),
        json::object(),
        json::object(),
        json{ { "prompt", "x? " }, { "password", false } },
        buffer_sequence());
    fakeKernel.sendStdinRequest(std::move(inputRequest), *kernelAuth);

    auto received = sc.client->receiveOnStdin(true);
    ASSERT_TRUE(received.has_value());
    EXPECT_EQ(received->header().at("msg_type").get<std::string>(), "input_request");
    EXPECT_EQ(received->content().at("prompt").get<std::string>(), "x? ");
}

TEST(ClientZmqTest, SendOnControlAndReceiveOnControlRoundTrips)
{
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);
    auto kernelAuth = makeAuthentication("hmac-sha256", kKey);

    StartedClient sc(makeConfig(fakeKernel));

    sc.client->sendOnControl(makeRequest("interrupt_request", json::object()));

    Message request = fakeKernel.recvControlRequest(*kernelAuth);
    EXPECT_EQ(request.header().at("msg_type").get<std::string>(), "interrupt_request");
    fakeKernel.sendControlReply(makeReplyTo(request, "interrupt_reply", { { "status", "ok" } }), *kernelAuth);

    auto result = sc.client->receiveOnControl(true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->header().at("msg_type").get<std::string>(), "interrupt_reply");
}

TEST(ClientZmqTest, ReceiveOnShellNonBlockingReturnsNulloptWhenNothingPending)
{
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);

    StartedClient sc(makeConfig(fakeKernel));

    EXPECT_FALSE(sc.client->receiveOnShell(false).has_value());
    EXPECT_FALSE(sc.client->receiveOnControl(false).has_value());
}

TEST(ClientZmqTest, WaitForMessageFallsBackToPollAndNotifiesShellListener)
{
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);
    auto kernelAuth = makeAuthentication("hmac-sha256", kKey);

    StartedClient sc(makeConfig(fakeKernel));

    std::atomic<bool> notified{ false };
    json notifiedContent;
    sc.client->registerShellListener([&](Message msg) {
        notifiedContent = msg.content();
        notified = true;
    });

    std::thread kernelThread([&]() {
        Message request = fakeKernel.recvShellRequest(*kernelAuth);
        fakeKernel.sendShellReply(makeReplyTo(request, "execute_reply", { { "status", "ok" } }), *kernelAuth);
    });

    sc.client->sendOnShell(makeRequest("execute_request", { { "code", "2 + 2" } }));

    // Iopub queue is empty, so this must fall through to poll(-1) internally
    // (client_zmq_impl.cpp's ClientZmqImpl::waitForMessage()) rather than
    // taking the popIopubMessage() branch covered by the iopub test below.
    sc.client->waitForMessage();

    kernelThread.join();
    EXPECT_TRUE(notified.load());
    EXPECT_EQ(notifiedContent.at("status").get<std::string>(), "ok");
}

TEST(ClientZmqTest, WaitForMessageFallsBackToPollAndNotifiesControlListener)
{
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);
    auto kernelAuth = makeAuthentication("hmac-sha256", kKey);

    StartedClient sc(makeConfig(fakeKernel));

    std::atomic<bool> notified{ false };
    sc.client->registerControlListener([&](Message /*msg*/) { notified = true; });

    std::thread kernelThread([&]() {
        Message request = fakeKernel.recvControlRequest(*kernelAuth);
        fakeKernel.sendControlReply(makeReplyTo(request, "interrupt_reply", { { "status", "ok" } }), *kernelAuth);
    });

    sc.client->sendOnControl(makeRequest("interrupt_request", json::object()));
    sc.client->waitForMessage();

    kernelThread.join();
    EXPECT_TRUE(notified.load());
}

TEST(ClientZmqTest, WaitForMessagePrefersAnAlreadyQueuedIopubMessage)
{
    zmq::context_t kernelCtx;
    FakeKernel fakeKernel(kernelCtx);
    auto kernelAuth = makeAuthentication("hmac-sha256", kKey);

    StartedClient sc(makeConfig(fakeKernel));

    // PUB/SUB has no connection handshake to wait on ("slow joiner"): keep
    // publishing until ClientIopub's background thread (already running via
    // start() above) has actually queued one, rather than assuming a single
    // publish lands.
    bool queued = waitFor([&]() {
        json header = makeHeader("stream", "kernel", "session-1");
        PubMessage msg("kernel.stream", header, json::object(), json::object(),
            json{ { "name", "stdout" }, { "text", "hi\n" } }, buffer_sequence());
        fakeKernel.publishIopub(std::move(msg), *kernelAuth);
        return sc.client->iopubQueueSize() > 0;
    }, 3000);
    ASSERT_TRUE(queued) << "iopub message never made it through the fake PUB/SUB link";

    std::atomic<bool> notified{ false };
    json notifiedContent;
    sc.client->registerIopubListener([&](PubMessage msg) {
        notifiedContent = msg.content();
        notified = true;
    });

    sc.client->waitForMessage();

    EXPECT_TRUE(notified.load());
    EXPECT_EQ(notifiedContent.at("name").get<std::string>(), "stdout");
}
