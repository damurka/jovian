#include "zmq.hpp"

#include "adrastea/context.hpp"

namespace adrastea
{
    std::unique_ptr<Context> makeZmqContext()
    {
        return std::unique_ptr<Context>(new ContextImpl<zmq::context_t>());
    }
}
