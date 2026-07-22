#include "datasuite/json.hpp"
#include "datasuite/control_messenger.hpp"

namespace datasuite
{
    control_messenger::~control_messenger()
    {
    }

    json control_messenger::send_to_shell(const json& message)
    {
        return send_to_shell_impl(message);
    }
}
