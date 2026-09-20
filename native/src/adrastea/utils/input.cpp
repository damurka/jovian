#include <stdexcept>
#include <string>

#include "adrastea/input.hpp"
#include "adrastea/interpreter.hpp"

namespace adrastea
{
    std::string blockingInputRequest(
        const std::string& prompt,
        bool password,
        bool allowStdin
    )
    {
        if (!allowStdin)
        {
            throw std::runtime_error(
                "This execution didn't allow interactive input (allow_stdin was false) -- "
                "the caller needs to opt in (e.g. execute(code, { allowStdin: true })) and be "
                "ready to answer an input_request for a blocking read like this to work.");
        }

        auto& interpreter = getInterpreter();

        // Register the input handler
        std::string value;
        interpreter.registerInputHandler([&value](const std::string& v) { value = v; });

        // Send the input request
        interpreter.inputRequest(prompt, password);

        // Remove input handler
        interpreter.registerInputHandler(nullptr);

        return value;
    }
}
