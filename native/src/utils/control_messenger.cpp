#include "datasuite/json.hpp"
#include "datasuite/control_messenger.hpp"

namespace datasuite
{
    ControlMessenger::~ControlMessenger()
    {
    }

    json ControlMessenger::sendToShell(const json& message)
    {
        return sendToShellImpl(message);
    }
}
