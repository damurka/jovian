#ifndef CALLISTO_STATA_TEXT_HPP
#define CALLISTO_STATA_TEXT_HPP

// The parts of the Stata kernel that are plain text processing -- reading
// Stata code and Stata's printed output -- kept apart from the embedding so
// they can be tested without a (licensed) Stata installation.

#include <string>
#include <vector>

namespace callisto { namespace text {

    // is_complete_request for Stata code: "incomplete" while a /* */ comment
    // or a { } block is still open or the last line ends in a /// line
    // continuation, "invalid" when a } closes nothing, otherwise "complete".
    // Strings, macros (`name') and comments are skipped when counting braces.
    std::string isComplete(const std::string& code);

    enum class CompletionKind
    {
        None,
        Variable, // a bare name: complete variable names in the dataset
        Global,   // after $ or ${
        Local,    // after a backtick
    };

    struct CompletionToken
    {
        CompletionKind kind = CompletionKind::None;
        // What has been typed of the name so far.
        std::string prefix;
        // Byte offset in the code where the name starts (the cursor_start of
        // the reply).
        int start = 0;
    };

    // What is being completed at cursorPos (a byte offset into code).
    CompletionToken completionToken(const std::string& code, int cursorPos);

    // The names from `candidates` that start with `prefix`, in order.
    std::vector<std::string> matchingNames(const std::vector<std::string>& candidates, const std::string& prefix);

    // Splits Stata's space-separated name lists (what invtokens() prints).
    std::vector<std::string> splitNames(const std::string& text);

    // The name under or just before the cursor (inspect_request).
    std::string tokenAt(const std::string& code, int cursorPos);

    // The error message Stata printed before the final "r(<rc>);" line of a
    // failed command's output ("variable nosuchvar not found"), or an empty
    // string if there is none.
    std::string errorMessage(const std::string& output, int rc);

    // Removes the leading and trailing whitespace (including newlines).
    std::string trim(const std::string& text);

} }

#endif
