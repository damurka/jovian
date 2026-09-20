#include "adrastea/json.hpp"
#include "adrastea/control_messenger.hpp"

namespace adrastea
{
    ControlMessenger::~ControlMessenger()
    {
    }

    json ControlMessenger::sendToShell(const json& message)
    {
        return sendToShellImpl(message);
    }
}
