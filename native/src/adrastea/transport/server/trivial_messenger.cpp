#include "trivial_messenger.hpp"

namespace adrastea
{
    TrivialMessenger::TrivialMessenger(listener l)
        : m_listener(std::move(l))
    {
    }

    json TrivialMessenger::sendToShellImpl(const json& message)
    {
        return m_listener(message);
    }
}
