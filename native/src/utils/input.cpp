#include <string>

#include "datasuite/input.hpp"
#include "datasuite/interpreter.hpp"

namespace datasuite
{
    std::string blockingInputRequest(
        const std::string& prompt,
        bool password
    )
    {
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
