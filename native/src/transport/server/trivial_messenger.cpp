#include "trivial_messenger.hpp"

namespace datasuite
{
    trivial_messenger::trivial_messenger(listener l)
        : m_listener(std::move(l))
    {
    }

    nl::json trivial_messenger::send_to_shell_impl(const nl::json& message)
    {
        return m_listener(message);
    }
}
