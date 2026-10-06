// The registration listener (native/src/adrastea/transport/client/client_handshake_zmq.cpp): the port it binds,
// and what it accepts. A kernel registers through sendConnectionInfo() (handshaking.cpp), here from a thread of
// this process; the happy path with a real kernel is SessionRegistryTest's.
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/transport/client/client_handshake_zmq.hpp"
#include "adrastea/transport/common/authentication.hpp"
#include "adrastea/transport/server/handshaking.hpp"

using namespace adrastea;

namespace
{
    RegistrationConfiguration listenerConfig(const std::string& key)
    {
        RegistrationConfiguration config;
        config.m_transport = "tcp";
        config.m_signatureScheme = "hmac-sha256";
        config.m_key = key;
        config.m_registrationIp = "127.0.0.1";
        config.m_registrationPort = ""; // empty: bound to a free port
        return config;
    }

    // What a kernel does once its sockets are bound: registers, with `key`, as `kernelId`, naming `ports`.
    // The listener's answer as it reaches the kernel: "" when accepted, else the refusal.
    std::string registerAs(const std::string& port, const std::string& key, const std::string& kernelId, const std::string& firstPort)
    {
        zmq::context_t context;
        RegistrationConfiguration regis = listenerConfig(key);
        regis.m_registrationPort = port;
        regis.m_kernelId = kernelId;
        KernelConfiguration kernel;
        kernel.m_key = key;
        kernel.m_controlPort = firstPort;
        kernel.m_shellPort = std::to_string(std::stoi(firstPort) + 1);
        kernel.m_stdinPort = std::to_string(std::stoi(firstPort) + 2);
        kernel.m_iopubPort = std::to_string(std::stoi(firstPort) + 3);
        kernel.m_hbPort = std::to_string(std::stoi(firstPort) + 4);
        auto auth = makeAuthentication("hmac-sha256", key);
        try
        {
            sendConnectionInfo(context, regis, kernel, *auth, json::error_handler_t::strict);
            return "";
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
    }
}

TEST(ClientHandshakeZmqTest, GetRegistrationPortReturnsTheAutoAssignedPort)
{
    auto context = makeZmqContext();
    ClientHandshakeZmq handshake(*context, listenerConfig("test-key"));

    std::string port = handshake.getRegistrationPort();
    EXPECT_FALSE(port.empty());
    EXPECT_NO_THROW((void)std::stoi(port));
}

TEST(ClientHandshakeZmqTest, ARegistrationIsAcceptedWithItsPortsAndId)
{
    auto context = makeZmqContext();
    ClientHandshakeZmq handshake(*context, listenerConfig("the-key"));
    const std::string port = handshake.getRegistrationPort();

    std::string answer;
    std::thread kernel([&] { answer = registerAs(port, "the-key", "launch-1", "40100"); });
    KernelConfiguration config = handshake.waitForConfiguration(nullptr, "launch-1");
    kernel.join();

    EXPECT_EQ(answer, "");
    EXPECT_EQ(config.m_kernelId, "launch-1");
    EXPECT_EQ(config.m_controlPort, "40100");
    EXPECT_EQ(config.m_hbPort, "40104");
    EXPECT_EQ(config.m_key, "the-key");
}

TEST(ClientHandshakeZmqTest, ARegistrationNotSignedWithTheListenersKeyIsRefusedAndTheNextOneTaken)
{
    // Nothing else on this machine may name a session's ports: a registration signed with another key is
    // refused, its sender is told so (and ends), and the wait goes on.
    auto context = makeZmqContext();
    ClientHandshakeZmq handshake(*context, listenerConfig("the-key"));
    const std::string port = handshake.getRegistrationPort();

    std::string wrongKey, rightKey;
    std::thread kernels([&] {
        wrongKey = registerAs(port, "another-key", "launch-1", "40200");
        rightKey = registerAs(port, "the-key", "launch-1", "40300");
    });
    KernelConfiguration config = handshake.waitForConfiguration(nullptr, "launch-1");
    kernels.join();

    // the refusal is signed with the listener's key, which this sender does not hold, so what it sees is an
    // answer it cannot verify -- either way, no supervisor
    EXPECT_NE(wrongKey.find("not signed with its key"), std::string::npos) << wrongKey;
    EXPECT_EQ(rightKey, "");
    EXPECT_EQ(config.m_controlPort, "40300") << "the refused registration's ports were not taken";
}

TEST(ClientHandshakeZmqTest, AnotherKernelsRegistrationIsRefusedAndTheAwaitedOneTaken)
{
    // The registration socket is one for every kernel: a registration from a kernel other than the one being
    // started (one of an earlier, timed-out launch registering late, say) must not be taken for it.
    auto context = makeZmqContext();
    ClientHandshakeZmq handshake(*context, listenerConfig("the-key"));
    const std::string port = handshake.getRegistrationPort();

    std::string stale, awaited;
    std::thread kernels([&] {
        stale = registerAs(port, "the-key", "launch-1", "40400");
        awaited = registerAs(port, "the-key", "launch-2", "40500");
    });
    KernelConfiguration config = handshake.waitForConfiguration(nullptr, "launch-2");
    kernels.join();

    EXPECT_NE(stale.find("another kernel's (launch-1, not launch-2)"), std::string::npos) << stale;
    EXPECT_EQ(awaited, "");
    EXPECT_EQ(config.m_kernelId, "launch-2");
    EXPECT_EQ(config.m_controlPort, "40500");
}

TEST(ClientHandshakeZmqTest, WithNoExpectedIdAnySignedRegistrationIsTaken)
{
    // A JEP 66 kernel (Ark) carries no id, so its supervisor expects none
    auto context = makeZmqContext();
    ClientHandshakeZmq handshake(*context, listenerConfig("the-key"));
    const std::string port = handshake.getRegistrationPort();

    std::string answer;
    std::thread kernel([&] { answer = registerAs(port, "the-key", "whatever", "40600"); });
    KernelConfiguration config = handshake.waitForConfiguration();
    kernel.join();

    EXPECT_EQ(answer, "");
    EXPECT_EQ(config.m_kernelId, "whatever");
    EXPECT_EQ(config.m_controlPort, "40600");
}
