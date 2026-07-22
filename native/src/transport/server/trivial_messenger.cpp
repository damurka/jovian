#include "trivial_messenger.hpp"

namespace datasuite
{
    TrivialMessenger::TrivialMessenger(listener l)
        : m_listener(std::move(l))
    {
    }

    json TrivialMessenger::send_to_shell_impl(const json& message)
    {
        return m_listener(message);
    }
}
