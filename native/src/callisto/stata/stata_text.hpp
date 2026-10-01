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
        Variable, // a bare name: variable names in the dataset, and scalars
        Global,   // after $ or ${
        Local,    // after a backtick
        Command,  // the first word of a command (after any prefix such as quietly or by ...:)
        Result,   // inside r( e( or s(: the names of stored results
    };

    struct CompletionToken
    {
        CompletionKind kind = CompletionKind::None;
        // What has been typed of the name so far.
        std::string prefix;
        // Byte offset in the code where the name starts (the cursor_start of
        // the reply).
        int start = 0;
        // For Result: 'r', 'e' or 's'.
        char resultClass = '\0';
    };

    // What is being completed at cursorPos (a byte offset into code).
    CompletionToken completionToken(const std::string& code, int cursorPos);

    // The names from `candidates` that start with `prefix`, in order.
    std::vector<std::string> matchingNames(const std::vector<std::string>& candidates, const std::string& prefix);

    // Splits Stata's space-separated name lists (what invtokens() prints).
    std::vector<std::string> splitNames(const std::string& text);

    // The lines of what Stata printed for a list printed one item per line
    // (names, paths), trimmed, without empty ones; a line Stata wrapped at
    // c(linesize) ("\n> " continuations) is joined first.
    std::vector<std::string> printedLines(const std::string& text);

    // Joins the lines Stata wrapped at c(linesize): removes each "\n> ".
    std::string unwrap(const std::string& text);

    // Stata's built-in commands (those that are not ado-files), for
    // completing the first word of a command; ado-file commands are found on
    // the adopath.
    const std::vector<std::string>& builtinCommands();

    // Whether a cell is one plain command that can be run as typed at Stata's
    // prompt (no do-file, so nothing is echoed): a single line, complete, no
    // comment, no #delimit, not exit.
    bool isSingleCommand(const std::string& code);

    // Turns Stata's output into valid UTF-8, a chunk at a time: a character
    // split across two chunks is held until the rest arrives, and a byte that
    // is not UTF-8 (text from a dataset saved by Stata 13 or older, in
    // Latin-1) is read as Latin-1. The kernel's messages must be valid UTF-8.
    class Utf8Decoder
    {
    public:
        std::string push(const std::string& bytes);
        // What is still held, at the end of the output.
        std::string finish();

    private:
        std::string m_pending;
    };

    // Holds back the last lines of a cell's output, so that when the cell
    // fails, the error message Stata printed last (and its "r(<rc>);") is not
    // shown twice -- in the output and in the error the kernel publishes. Lines
    // older than the last few go out as they come, and the held ones too once
    // the output has been quiet a moment (release()).
    class TailHold
    {
    public:
        static constexpr std::size_t kLines = 8;

        // What may go out now of `text` added to what is held
        std::string push(const std::string& text);
        // Everything held (the output has been quiet)
        std::string release();
        // The end of the output: everything held, without `error` (the
        // message and "r(<rc>);" lines) when it ends with it
        std::string finish(const std::string& error);

    private:
        std::string m_held;
    };

    // Removes what Stata echoes when it runs a do-file -- the commands
    // (". cmd", their "> " continuations, the numbered lines of a block or
    // program), the blank line before each, the final prompt, the return code
    // repeated after an error -- from its output, a chunk at a time, so a cell
    // shows only what its commands print, as at Stata's own prompt. Text that
    // cannot be part of an echo goes out at once, before its line ends.
    class EchoFilter
    {
    public:
        std::string push(const std::string& text);
        // The end of the output: what is held is either echo or goes out.
        std::string finish();

    private:
        std::string line(const std::string& text);
        bool mayBeEcho(const std::string& partial) const;

        std::string m_partial;      // the current line, not yet classified
        bool m_passing = false;     // the current line was found to be output: the rest goes out as it comes
        int m_blankLines = 0;       // blank lines held until we know whether an echo follows
        bool m_afterEcho = false;   // the last line was a command echo (a "> " continuation may follow)
        bool m_numbered = false;    // the lines of a block or program are being echoed ("  2. ...")
        bool m_continuable = false; // the last line was a numbered one ("> " may continue it)
        std::string m_command;      // the command echoed last, continuations joined
        std::string m_lastLine;     // the last line that went out
    };

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
