#include "zmq.hpp"

#include "datasuite/context.hpp"

namespace datasuite
{
    std::unique_ptr<Context> make_zmq_context()
    {
        return std::unique_ptr<Context>(new ContextImpl<zmq::context_t>());
    }
}
