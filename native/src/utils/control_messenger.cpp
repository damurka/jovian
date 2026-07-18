#include "nlohmann/json.hpp"

#include "datasuite/control_messenger.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    control_messenger::~control_messenger()
    {
    }

    nl::json control_messenger::send_to_shell(const nl::json& message)
    {
        return send_to_shell_impl(message);
    }
}
