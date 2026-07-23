#ifndef DATASUITE_TRIVIAL_MESSENGER_HPP
#define DATASUITE_TRIVIAL_MESSENGER_HPP

#include <functional>

#include "datasuite/json.hpp"

#include "datasuite/control_messenger.hpp"

namespace datasuite
{
    class ServerZmqDefault;

    class TrivialMessenger : public ControlMessenger
    {
    public:

        using listener = std::function<json(json)>;

        explicit TrivialMessenger(listener l);
        virtual ~TrivialMessenger() = default;

    private:

        json sendToShellImpl(const json& message) override;

        listener m_listener;
    };
}

#endif
