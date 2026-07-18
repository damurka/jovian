#ifndef DATASUITE_HELPER_HPP
#define DATASUITE_HELPER_HPP

#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp"
#include "kernel_configuration.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    DATASUITE_API std::string get_start_message(const kernel_configuration& config);

    /**
     * @brief Extracts the filename from the command-line arguments and adjusts argc/argv.
     *
     * Searches for the "-f" flag in the arguments, extracts the following filename, and
     * removes both from the argument list. `argc` is updated to reflect the changes.
     * @param argc Reference to the argument count, modified if "-f" is found.
     * @param argv Argument list, potentially modified.
     * @return The extracted filename, or an empty string if not found.
     */
    DATASUITE_API std::string extract_filename(int &argc, char* argv[]);

    DATASUITE_API bool should_print_version(int argc, char* argv[]);

    DATASUITE_API
    nl::json create_error_reply(const std::string& ename = std::string(),
                                const std::string& evalue = std::string(),
                                const nl::json& trace_back = nl::json::array());

    DATASUITE_API
    nl::json create_successful_reply(const nl::json& payload = nl::json::array(),
                                     const nl::json& user_expressions = nl::json::object());

    DATASUITE_API
    nl::json create_complete_reply(const nl::json& matches,
                                   const int& cursor_start,
                                   const int& cursor_end,
                                   const nl::json& metadata = nl::json::object());

    DATASUITE_API
    nl::json create_inspect_reply(const bool found = false,
                                  const nl::json& data = nl::json::object(),
                                  const nl::json& metadata = nl::json::object());

    DATASUITE_API
    nl::json create_is_complete_reply(const std::string& status = std::string(),
                                      const std::string& indent = std::string(""));

    using codemirror_mode_t = std::variant<std::string, nl::json>;

    DATASUITE_API
    nl::json create_info_reply(const std::string& implementation = std::string(),
                               const std::string& implementation_version = std::string(),
                               const std::string& language_name = std::string(),
                               const std::string& language_version = std::string(),
                               const std::string& language_mimetype = std::string(),
                               const std::string& language_file_extension = std::string(),
                               const std::string& pygments_lexer = std::string(),
                               codemirror_mode_t language_codemirror_mode = std::string(),
                               const std::string& language_nbconvert_exporter = std::string(),
                               const std::string& banner = std::string(),
                               const nl::json& help_links = nl::json::array(),
                               const std::vector<std::string>& supported_features = std::vector<std::string>());

    DATASUITE_API
    nl::json create_shutdown_reply(bool restart);

    DATASUITE_API
    nl::json create_interrupt_reply();
}

#endif