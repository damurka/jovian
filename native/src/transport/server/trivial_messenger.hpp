#ifndef ADRASTEA_TRIVIAL_MESSENGER_HPP
#define ADRASTEA_TRIVIAL_MESSENGER_HPP

#include <functional>

#include "adrastea/json.hpp"

#include "adrastea/control_messenger.hpp"

namespace adrastea
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
