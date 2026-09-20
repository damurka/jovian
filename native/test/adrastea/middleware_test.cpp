// Pure-function coverage for native/src/adrastea/transport/common/middleware.cpp --
// none of this needs R, ZMQ sockets, or a spawned process, so it's fast and
// has no reason to share fixture/skip machinery with session_registry_test.
#include <set>
#include <string>

#include <gtest/gtest.h>
#include <zmq.hpp>

#include "adrastea/middleware.hpp"
#include "adrastea/transport/common/middleware_impl.hpp"

using namespace adrastea;

TEST(MiddlewareTest, GetEndPointFormatsTcpAsHostColonPort)
{
    EXPECT_EQ(getEndPoint("tcp", "127.0.0.1", "5555"), "tcp://127.0.0.1:5555");
}

TEST(MiddlewareTest, GetEndPointFormatsNonTcpWithDashSeparator)
{
    // See middleware.cpp: transport != "tcp" (e.g. "inproc") uses '-'
    // instead of ':' between ip and port.
    EXPECT_EQ(getEndPoint("inproc", "publisher", "0"), "inproc://publisher-0");
}

TEST(MiddlewareTest, GetControllerEndPointIsInprocNamedAfterChannel)
{
    EXPECT_EQ(getControllerEndPoint("iopub"), "inproc://iopub_controller");
    EXPECT_EQ(getControllerEndPoint("heartbeat"), "inproc://heartbeat_controller");
}

TEST(MiddlewareTest, GetPublisherEndPointIsFixed)
{
    EXPECT_EQ(getPublisherEndPoint(), "inproc://publisher");
}

TEST(MiddlewareTest, GetSocketLingerIsBoundedNotInfinite)
{
    // Documents the actual contract callers rely on (e.g.
    // client_messenger.cpp uses this for the registration/controller
    // sockets): a finite linger, not ZMQ's default of -1 (infinite), which
    // would make socket teardown able to hang indefinitely.
    EXPECT_GT(getSocketLinger(), 0);
}

TEST(MiddlewareTest, FindFreePortReturnsAPortInTheRequestedRange)
{
    std::string port = findFreePort(100, 49152, 65535);
    ASSERT_FALSE(port.empty());
    int portNum = std::stoi(port);
    EXPECT_GE(portNum, 49152);
    EXPECT_LE(portNum, 65535);
}

TEST(MiddlewareTest, InitSocketFallsBackToAFreshPortWhenThePreSelectedOneIsTaken)
{
    // Regression test for a real Ubuntu CI failure
    // (SessionRegistryTest.ConcurrentRestartsForTheSameSessionDontLeakAnExtraKernelProcess):
    // findFreePort() only proves a port is free at the moment it probes it
    // (bind, then immediately unbind); the real bind against that port
    // number happens much later (after R interpreter init), leaving a
    // window where another concurrently-spawned kernel process can take
    // it first. initSocket() must not let that surface as an unhandled
    // "Address already in use" -- it should recover by picking a fresh
    // port on the same socket, the same race-free path used when no port
    // was requested at all.
    std::string stalePort = findFreePort();

    zmq::context_t ctx;
    zmq::socket_t occupier(ctx, zmq::socket_type::req);
    occupier.bind(getEndPoint("tcp", "127.0.0.1", stalePort));

    zmq::socket_t contender(ctx, zmq::socket_type::req);
    EXPECT_NO_THROW(initSocket(contender, "tcp", "127.0.0.1", stalePort));

    EXPECT_NE(getSocketPort(contender), stalePort);
}

TEST(MiddlewareTest, FindFreePortReturnsDistinctPortsAcrossCalls)
{
    // Not a strict guarantee (it's a random search over a large range), but
    // a regression check: if findFreePort() ever started always returning
    // the same port (e.g. a broken RNG seed), every session/kernel spawned
    // in the same process would collide -- exactly the bug findFreePort()
    // exists to prevent (see its call sites' comments in
    // elara.cpp/engine.cpp).
    std::set<std::string> ports;
    for (int i = 0; i < 5; ++i)
    {
        ports.insert(findFreePort());
    }
    EXPECT_EQ(ports.size(), 5u);
}
