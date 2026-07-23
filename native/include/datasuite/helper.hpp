#ifndef DATASUITE_HELPER_HPP
#define DATASUITE_HELPER_HPP

#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp"
#include "kernel_configuration.hpp"

namespace datasuite
{
    DATASUITE_API std::string getStartMessage(const KernelConfiguration& config);

    /**
     * @brief Extracts the filename from the command-line arguments and adjusts argc/argv.
     *
     * Searches for the "-f" flag in the arguments, extracts the following filename, and
     * removes both from the argument list. `argc` is updated to reflect the changes.
     * @param argc Reference to the argument count, modified if "-f" is found.
     * @param argv Argument list, potentially modified.
     * @return The extracted filename, or an empty string if not found.
     */
    DATASUITE_API std::string extractFilename(int &argc, char* argv[]);

    DATASUITE_API bool shouldPrintVersion(int argc, char* argv[]);

    DATASUITE_API
    json createErrorReply(const std::string& ename = std::string(),
                                const std::string& evalue = std::string(),
                                const json& trace_back = json::array());

    DATASUITE_API
    json createSuccessfulReply(const json& payload = json::array(),
                                     const json& user_expressions = json::object());

    DATASUITE_API
    json createCompleteReply(const json& matches,
                                   const int& cursor_start,
                                   const int& cursor_end,
                                   const json& metadata = json::object());

    DATASUITE_API
    json createInspectReply(const bool found = false,
                                  const json& data = json::object(),
                                  const json& metadata = json::object());

    DATASUITE_API
    json createIsCompleteReply(const std::string& status = std::string(),
                                      const std::string& indent = std::string(""));

    using codemirror_mode_t = std::variant<std::string, json>;

    DATASUITE_API
    json createInfoReply(const std::string& implementation = std::string(),
                               const std::string& implementation_version = std::string(),
                               const std::string& language_name = std::string(),
                               const std::string& language_version = std::string(),
                               const std::string& language_mimetype = std::string(),
                               const std::string& language_file_extension = std::string(),
                               const std::string& pygments_lexer = std::string(),
                               codemirror_mode_t language_codemirror_mode = std::string(),
                               const std::string& language_nbconvert_exporter = std::string(),
                               const std::string& banner = std::string(),
                               const json& help_links = json::array(),
                               const std::vector<std::string>& supported_features = std::vector<std::string>());

    DATASUITE_API
    json createShutdownReply(bool restart);

    DATASUITE_API
    json createInterruptReply();
}

#endif