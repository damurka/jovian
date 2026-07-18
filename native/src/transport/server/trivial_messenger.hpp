#ifndef DATASUITE_TRIVIAL_MESSENGER_HPP
#define DATASUITE_TRIVIAL_MESSENGER_HPP

#include <functional>

#include "nlohmann/json.hpp"

#include "datasuite/control_messenger.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    class server_zmq_default;

    class trivial_messenger : public control_messenger
    {
    public:

        using listener = std::function<nl::json(nl::json)>;

        explicit trivial_messenger(listener l);
        virtual ~trivial_messenger() = default;

    private:

        nl::json send_to_shell_impl(const nl::json& message) override;

        listener m_listener;
    };
}

#endif
