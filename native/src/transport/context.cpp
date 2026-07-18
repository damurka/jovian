#include "zmq.hpp"

#include "datasuite/context.hpp"

namespace datasuite
{
    std::unique_ptr<context> make_zmq_context()
    {
        return std::unique_ptr<context>(new context_impl<zmq::context_t>());
    }
}
