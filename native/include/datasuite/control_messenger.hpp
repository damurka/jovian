#ifndef DATASUITE_CONTROL_MESSENGER_HPP
#define DATASUITE_CONTROL_MESSENGER_HPP

#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
    class DATASUITE_API ControlMessenger
    {
    public:

        virtual ~ControlMessenger();

        ControlMessenger(const ControlMessenger&) = delete;
        ControlMessenger& operator=(const ControlMessenger&) = delete;

        ControlMessenger(ControlMessenger&&) = delete;
        ControlMessenger& operator=(ControlMessenger&&) = delete;

        json sendToShell(const json& message);

    protected:

        ControlMessenger() = default;

    private:

        virtual json sendToShellImpl(const json& message) = 0;
    };
}

#endif
