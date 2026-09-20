// Coverage for ClientHandshakeZmq::getRegistrationPort()
// (native/src/adrastea/transport/client/client_handshake_zmq.cpp) -- a public method
// with zero callers in this codebase today (SessionRegistry::
// startRegistrationListener() tracks the port itself, from its own prior
// findFreePort() call, rather than asking the handshake object). Still part
// of the public API surface, so worth covering directly. The registration
// handshake's happy path itself (waitForConfiguration() receiving a real
// kernel's registration) is already covered end-to-end by
// SessionRegistryTest's real elara.exe kernel.
#include <stdexcept>

#include <gtest/gtest.h>

#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/transport/client/client_handshake_zmq.hpp"

using namespace adrastea;

TEST(ClientHandshakeZmqTest, GetRegistrationPortReturnsTheAutoAssignedPort)
{
    RegistrationConfiguration config;
    config.m_transport = "tcp";
    config.m_signatureScheme = "hmac-sha256";
    config.m_key = "test-key";
    config.m_registrationIp = "127.0.0.1";
    config.m_registrationPort = ""; // empty -> auto-bind to a free port

    auto context = makeZmqContext();
    ClientHandshakeZmq handshake(*context, config);

    std::string port = handshake.getRegistrationPort();
    EXPECT_FALSE(port.empty());
    EXPECT_NO_THROW((void)std::stoi(port));
}
