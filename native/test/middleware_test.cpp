// Pure-function coverage for native/src/transport/common/middleware.cpp --
// none of this needs R, ZMQ sockets, or a spawned process, so it's fast and
// has no reason to share fixture/skip machinery with session_registry_test.
#include <set>
#include <string>

#include <gtest/gtest.h>

#include "adrastea/middleware.hpp"

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
