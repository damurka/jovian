#include <string>

#include "datasuite/input.hpp"
#include "datasuite/interpreter.hpp"

namespace datasuite
{
    std::string blocking_input_request(
        const std::string& prompt,
        bool password
    )
    {
        auto& interpreter = get_interpreter();

        // Register the input handler
        std::string value;
        interpreter.register_input_handler([&value](const std::string& v) { value = v; });

        // Send the input request
        interpreter.input_request(prompt, password);

        // Remove input handler
        interpreter.register_input_handler(nullptr);

        return value;
    }
}
