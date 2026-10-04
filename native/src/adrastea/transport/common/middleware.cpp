#include <string>
#include <stdexcept>
#include <random>
#include <cerrno>

#include "zmq_addon.hpp"
#include "middleware_impl.hpp"

namespace adrastea
{
    std::string getControllerEndPoint(const std::string& channel)
    {
        return "inproc://" + channel + "_controller";
    }

    std::string getPublisherEndPoint()
    {
        return "inproc://publisher";
    }

    std::string getEndPoint(const std::string& transport,
        const std::string& ip,
        const std::string& port)
    {
        char sep = (transport == "tcp") ? ':' : '-';
        return transport + "://" + ip + sep + port;
    }

    int getSocketLinger()
    {
        return 1000;
    }

    std::string findFreePortImpl(zmq::socket_t& socket,
        const std::string& transport,
        const std::string& ip,
        std::size_t max_tries,
        int start,
        int stop)
    {
        std::random_device r;
        std::default_random_engine generator(r());
        std::uniform_int_distribution<int> distribution(start, stop);
        std::size_t tries(0);
        std::string rd_port;

        do
        {
            rd_port = std::to_string(distribution(generator));
        } while (++tries <= max_tries && zmq_bind(socket, getEndPoint(transport, ip, rd_port).c_str()) != 0);

        if (tries > max_tries)
        {
            rd_port = "";
        }

        return rd_port;
    }

    void initSocket(zmq::socket_t& socket,
        const std::string& transport,
        const std::string& ip,
        const std::string& port)
    {
        socket.set(zmq::sockopt::linger, getSocketLinger());

        if (!port.empty())
        {
            // A port that was given is one somebody else chose and already knows (a Jupyter connection file: the
            // launcher dials it): this socket is bound to it or the start fails. Binding another instead left a
            // kernel that said "ready" on a channel nobody could reach.
            try
            {
                socket.bind(getEndPoint(transport, ip, port));
            }
            catch (const zmq::error_t& e)
            {
                throw std::runtime_error("cannot bind " + getEndPoint(transport, ip, port) + ": " + e.what()
                    + " (the port was chosen by whoever started this process, and something else has it)");
            }
        }
        else
        {
            // No port given: this socket takes a free one itself and keeps it (no probe, so nothing can take it
            // in between); whoever needs it asks for it afterwards (getSocketPort()). Jovian's kernels and its
            // supervisor start this way, and report the ports they are bound to.
            if (findFreePortImpl(socket, transport, ip, 100, 49152, 65536).empty())
            {
                throw std::runtime_error("no free port found on " + ip);
            }
        }
    }

    void initSocket(zmq::socket_t& socket, const std::string& end_point)
    {
        socket.set(zmq::sockopt::linger, getSocketLinger());
        socket.bind(end_point);
    }

    std::string getSocketPort(const zmq::socket_t& socket)
    {
        std::string end_point = socket.get(zmq::sockopt::last_endpoint, 32);
        return end_point.substr(end_point.find_last_of(":") + 1);
    }

    std::string findFreePort(std::size_t max_tries, int start, int stop)
    {
        static const std::string transport = "tcp";
        static const std::string ip = "127.0.0.1";
        zmq::context_t ctx;
        zmq::socket_t socket(ctx, zmq::socket_type::req);
        std::string port = findFreePortImpl(socket, transport, ip, max_tries, start, stop);
        socket.unbind(getEndPoint(transport, ip, port));
        return port;
    }
}
