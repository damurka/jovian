#ifndef DATASUITE_TRIVIAL_MESSENGER_HPP
#define DATASUITE_TRIVIAL_MESSENGER_HPP

#include <functional>

#include "datasuite/json.hpp"

#include "datasuite/control_messenger.hpp"

namespace datasuite
{
    class server_zmq_default;

    class trivial_messenger : public control_messenger
    {
    public:

        using listener = std::function<json(json)>;

        explicit trivial_messenger(listener l);
        virtual ~trivial_messenger() = default;

    private:

        json send_to_shell_impl(const json& message) override;

        listener m_listener;
    };
}

#endif
