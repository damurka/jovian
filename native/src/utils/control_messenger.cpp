#include "datasuite/json.hpp"
#include "datasuite/control_messenger.hpp"

namespace datasuite
{
    ControlMessenger::~ControlMessenger()
    {
    }

    json ControlMessenger::send_to_shell(const json& message)
    {
        return send_to_shell_impl(message);
    }
}
