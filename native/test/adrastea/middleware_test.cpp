// Pure-function coverage for native/src/adrastea/transport/common/middleware.cpp --
// none of this needs R, ZMQ sockets, or a spawned process, so it's fast and
// has no reason to share fixture/skip machinery with session_registry_test.
#include <set>
#include <string>
#include <stdexcept>
#include <vector>

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

TEST(MiddlewareTest, InitSocketFailsWhenTheGivenPortIsTaken)
{
    // A given port is one its chooser already knows (a Jupyter connection file): binding another instead left a
    // kernel "ready" on a channel nobody could reach. It is an error, which names the endpoint.
    zmq::context_t ctx;
    zmq::socket_t occupier(ctx, zmq::socket_type::req);
    initSocket(occupier, "tcp", "127.0.0.1", "");
    std::string taken = getSocketPort(occupier);

    zmq::socket_t contender(ctx, zmq::socket_type::req);
    try
    {
        initSocket(contender, "tcp", "127.0.0.1", taken);
        FAIL() << "bound a port that was taken";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("127.0.0.1:" + taken), std::string::npos) << e.what();
    }
}

TEST(MiddlewareTest, InitSocketWithNoPortBindsAFreeOneAndKeepsIt)
{
    // How Jovian's kernels and supervisor start: nothing is probed, each socket binds its own port, and that
    // port is what gets reported. Twenty sockets, twenty different ports, all still bound.
    zmq::context_t ctx;
    std::vector<zmq::socket_t> sockets;
    std::set<std::string> ports;
    for (int i = 0; i < 20; ++i)
    {
        sockets.emplace_back(ctx, zmq::socket_type::req);
        initSocket(sockets.back(), "tcp", "127.0.0.1", "");
        std::string port = getSocketPort(sockets.back());
        EXPECT_FALSE(port.empty());
        EXPECT_TRUE(ports.insert(port).second) << port << " bound twice";
    }
}

TEST(MiddlewareTest, FindFreePortDoesNotKeepReturningTheSamePort)
{
    // A regression check: if findFreePort() ever started always returning
    // the same port (e.g. a broken RNG seed), every session/kernel spawned
    // in the same process would collide -- exactly the bug findFreePort()
    // exists to prevent (see its call sites' comments in
    // elara.cpp/engine.cpp).
    //
    // Not "five calls, five ports": the ports are drawn at random from about
    // 16,000, so two of five are the same about once in 1,600 runs -- which
    // failed a release's build (a collision is harmless: initSocket() binds
    // another port, and the kernel announces the one it bound). Twenty calls
    // all but never share more than a few; a stuck generator shares all.
    std::set<std::string> ports;
    for (int i = 0; i < 20; ++i)
    {
        ports.insert(findFreePort());
    }
    EXPECT_GE(ports.size(), 15u);
}
