// Coverage for ClientHeartbeat (native/src/transport/client/client_heartbeat.cpp),
// tested standalone rather than through ClientZmqImpl -- its real timeout
// (20s) and max_retry (3) are hardcoded constants in client_zmq_impl.cpp,
// making the dead-kernel-detection path impractical to exercise through the
// full client stack. ClientHeartbeat's own constructor takes both as plain
// parameters, so it's directly testable with timeouts small enough to run
// in milliseconds.
//
// This test also exercises the fix for a real bug found while writing it:
// waitForAnswer() used to return `true` unconditionally whenever poll()
// didn't throw, regardless of whether a pong actually arrived -- which made
// notifyKernelDead() unreachable from a real missed heartbeat (see the fix
// in client_heartbeat.cpp and its comment).
#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "datasuite/kernel_configuration.hpp"
#include "datasuite/middleware.hpp"
#include "transport/client/client_heartbeat.hpp"

using namespace datasuite;

namespace
{
    KernelConfiguration makeConfig(const std::string& hbPort)
    {
        KernelConfiguration config;
        config.m_transport = "tcp";
        config.m_ip = "127.0.0.1";
        config.m_hbPort = hbPort;
        return config;
    }

    // Binds a REP-shaped responder to `port` and echoes anything it
    // receives back until `stop` is set -- stands in for a live kernel
    // answering heartbeat pings.
    class RespondingKernel
    {
    public:
        RespondingKernel(zmq::context_t& ctx, const std::string& port)
            : m_socket(ctx, zmq::socket_type::rep)
        {
            m_socket.bind(getEndPoint("tcp", "127.0.0.1", port));
            m_socket.set(zmq::sockopt::rcvtimeo, 50);
        }

        void runUntil(std::atomic<bool>& stop)
        {
            while (!stop.load())
            {
                zmq::message_t ping;
                if (m_socket.recv(ping))
                {
                    zmq::message_t pong("pong", 4);
                    m_socket.send(pong, zmq::send_flags::none);
                }
            }
        }

    private:
        zmq::socket_t m_socket;
    };
}

TEST(ClientHeartbeatTest, StaysAliveWhenKernelRespondsToEveryPing)
{
    zmq::context_t ctx;
    std::string port = "38001";
    RespondingKernel kernel(ctx, port);

    std::atomic<bool> stopKernel{ false };
    std::thread kernelThread([&]() { kernel.runUntil(stopKernel); });

    ClientHeartbeat heartbeat(ctx, makeConfig(port), /*max_retry=*/1, /*timeout=*/100);

    std::atomic<bool> deadNotified{ false };
    heartbeat.registerKernelStatusListener([&](bool dead) { deadNotified = dead; });

    std::thread heartbeatThread([&]() { heartbeat.run(); });

    // A few ping/pong cycles' worth of runway (100ms timeout + 100ms sleep
    // per run() iteration) -- long enough to prove it doesn't misfire, not
    // trying to prove it never would given more time.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    zmq::socket_t stopSock(ctx, zmq::socket_type::req);
    stopSock.connect(getControllerEndPoint("heartbeat"));
    zmq::message_t stopMsg("stop", 4);
    stopSock.send(stopMsg, zmq::send_flags::none);
    zmq::message_t stopResp;
    (void)stopSock.recv(stopResp);

    heartbeatThread.join();
    stopKernel = true;
    kernelThread.join();

    EXPECT_FALSE(deadNotified.load());
}

TEST(ClientHeartbeatTest, NotifiesKernelDeadAfterMaxRetryUnansweredPings)
{
    zmq::context_t ctx;
    // Nothing bound on this port at all -- every ping goes unanswered. Also
    // exercises the m_heartbeat linger=0 fix (client_heartbeat.cpp): without
    // it, this socket's undelivered pings block indefinitely on close/
    // context teardown at the end of this test, since nothing was ever
    // listening to actually receive them.
    ClientHeartbeat heartbeat(ctx, makeConfig("38002"), /*max_retry=*/1, /*timeout=*/100);

    std::atomic<bool> deadNotified{ false };
    heartbeat.registerKernelStatusListener([&](bool dead) { deadNotified = dead; });

    std::thread heartbeatThread([&]() { heartbeat.run(); });

    // run() should self-terminate (break out of its loop) once max_retry+1
    // unanswered cycles have elapsed -- no controller stop signal needed.
    heartbeatThread.join();

    EXPECT_TRUE(deadNotified.load());
}
