#ifndef DATASUITE_CONTROL_MESSENGER_HPP
#define DATASUITE_CONTROL_MESSENGER_HPP

#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
    class DATASUITE_API control_messenger
    {
    public:

        virtual ~control_messenger();

        control_messenger(const control_messenger&) = delete;
        control_messenger& operator=(const control_messenger&) = delete;

        control_messenger(control_messenger&&) = delete;
        control_messenger& operator=(control_messenger&&) = delete;

        json send_to_shell(const json& message);

    protected:

        control_messenger() = default;

    private:

        virtual json send_to_shell_impl(const json& message) = 0;
    };
}

#endif
