#include "callisto/stata/stata_text.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <set>
#include <sstream>

namespace callisto { namespace text {

    namespace
    {
        bool isSpace(char c)
        {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        }

        // Stata names are letters, digits and underscores; bytes of a UTF-8
        // sequence count too (Stata 14+ allows Unicode names).
        bool isNameChar(char c)
        {
            unsigned char u = static_cast<unsigned char>(c);
            return u >= 0x80 || std::isalnum(u) || c == '_';
        }

        std::vector<std::string> splitLines(const std::string& text)
        {
            std::vector<std::string> lines;
            std::string line;
            std::istringstream stream(text);
            while (std::getline(stream, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
            return lines;
        }
    }

    std::string trim(const std::string& text)
    {
        std::size_t begin = 0;
        std::size_t end = text.size();
        while (begin < end && isSpace(text[begin])) ++begin;
        while (end > begin && isSpace(text[end - 1])) --end;
        return text.substr(begin, end - begin);
    }

    std::string isComplete(const std::string& code)
    {
        int commentDepth = 0; // /* */ comments nest in Stata
        int braces = 0;
        bool continued = false; // the last line with anything on it ended in ///

        for (const std::string& line : splitLines(code))
        {
            if (commentDepth == 0 && trim(line).empty()) continue;
            continued = false;

            bool inString = false; // "..."
            int compound = 0;      // `"..."' (these nest)
            bool sawCode = false;
            std::size_t n = line.size();
            for (std::size_t j = 0; j < n;)
            {
                char c = line[j];
                char next = j + 1 < n ? line[j + 1] : '\0';

                if (commentDepth > 0)
                {
                    if (c == '*' && next == '/') { --commentDepth; j += 2; }
                    else if (c == '/' && next == '*') { ++commentDepth; j += 2; }
                    else ++j;
                    continue;
                }
                if (inString)
                {
                    if (c == '"') inString = false;
                    ++j;
                    continue;
                }
                if (compound > 0)
                {
                    if (c == '`' && next == '"') { ++compound; j += 2; }
                    else if (c == '"' && next == '\'') { --compound; j += 2; }
                    else ++j;
                    continue;
                }
                if (isSpace(c)) { ++j; continue; }

                // A line whose first character is * is a comment.
                if (!sawCode && c == '*') break;
                if (c == '/' && next == '*') { ++commentDepth; j += 2; continue; }
                // // and /// start a comment only at the start of a line or after a blank.
                if (c == '/' && next == '/' && (j == 0 || isSpace(line[j - 1])))
                {
                    if (j + 2 < n && line[j + 2] == '/') continued = true;
                    break;
                }

                sawCode = true;
                if (c == '`' && next == '"') { compound = 1; j += 2; continue; }
                if (c == '"') inString = true;
                else if (c == '{') ++braces;
                else if (c == '}' && --braces < 0) return "invalid";
                ++j;
            }
        }

        if (commentDepth > 0 || braces > 0 || continued) return "incomplete";
        return "complete";
    }

    namespace
    {
        // Prefixes that may come before a command: quietly regress ...
        bool isCommandPrefix(const std::string& word)
        {
            static const std::set<std::string> prefixes = { "quietly", "qui", "quietl", "noisily", "noi", "capture",
                "cap", "captur", "capt", "captu", "bysort", "bys", "by", "xi", "svy", "mi", "statsby", "rolling",
                "bootstrap", "jackknife", "permute", "simulate", "nestreg", "stepwise", "eststo", "version", "frame",
                "timer", "python", "mata", "collect", "table" };
            return prefixes.count(word) > 0;
        }

        // Whether the name starting at `start` is the first word of a command:
        // only blanks before it on its line, a prefix (quietly) or a "by ...:"
        bool startsCommand(const std::string& code, int start)
        {
            int lineStart = start;
            while (lineStart > 0 && code[lineStart - 1] != '\n' && code[lineStart - 1] != ';') --lineStart;
            std::string before = code.substr(lineStart, start - lineStart);
            // what follows the last colon of a prefix ("by foreign: summ", "quietly: reg")
            std::size_t colon = before.rfind(':');
            if (colon != std::string::npos) before = before.substr(colon + 1);
            std::istringstream words(before);
            std::string word;
            while (words >> word)
            {
                if (!isCommandPrefix(word)) return false;
            }
            return true;
        }
    }

    CompletionToken completionToken(const std::string& code, int cursorPos)
    {
        int pos = std::clamp(cursorPos, 0, static_cast<int>(code.size()));
        int start = pos;
        while (start > 0 && isNameChar(code[start - 1])) --start;

        CompletionToken token;
        token.start = start;
        token.prefix = code.substr(start, pos - start);

        char before = start > 0 ? code[start - 1] : '\0';
        char beforeThat = start > 1 ? code[start - 2] : '\0';
        char third = start > 2 ? code[start - 3] : '\0';
        if (before == '`') token.kind = CompletionKind::Local;
        else if (before == '$' || (before == '{' && beforeThat == '$')) token.kind = CompletionKind::Global;
        else if (before == '(' && (beforeThat == 'r' || beforeThat == 'e' || beforeThat == 's') && !isNameChar(third))
        {
            token.kind = CompletionKind::Result;
            token.resultClass = beforeThat;
        }
        else if (!token.prefix.empty() && !std::isdigit(static_cast<unsigned char>(token.prefix[0])))
            token.kind = startsCommand(code, start) ? CompletionKind::Command : CompletionKind::Variable;
        return token;
    }

    std::vector<std::string> matchingNames(const std::vector<std::string>& candidates, const std::string& prefix)
    {
        std::vector<std::string> matches;
        std::set<std::string> seen;
        for (const auto& name : candidates)
        {
            if (name.compare(0, prefix.size(), prefix) == 0 && seen.insert(name).second)
            {
                matches.push_back(name);
            }
        }
        return matches;
    }

    std::vector<std::string> splitNames(const std::string& text)
    {
        std::vector<std::string> names;
        std::istringstream stream(text);
        std::string name;
        while (stream >> name) names.push_back(name);
        return names;
    }

    std::string unwrap(const std::string& text)
    {
        std::string joined;
        joined.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            // "\n> " (or "\r\n> ") continues the line before
            if (text[i] == '\r' && i + 3 < text.size() && text[i + 1] == '\n' && text[i + 2] == '>' && text[i + 3] == ' ')
            {
                i += 3;
                continue;
            }
            if (text[i] == '\n' && i + 2 < text.size() && text[i + 1] == '>' && text[i + 2] == ' ')
            {
                i += 2;
                continue;
            }
            joined += text[i];
        }
        return joined;
    }

    std::vector<std::string> printedLines(const std::string& text)
    {
        std::vector<std::string> lines;
        for (const std::string& line : splitLines(unwrap(text)))
        {
            std::string trimmed = trim(line);
            if (!trimmed.empty()) lines.push_back(trimmed);
        }
        return lines;
    }

    const std::vector<std::string>& builtinCommands()
    {
        // Built into Stata's executable (not ado-files), from Stata 18's
        // manuals; the ado-file commands come from the adopath.
        static const std::vector<std::string> commands = {
            "about", "adopath", "anova", "append", "args", "assert", "break", "by", "bysort", "capture", "cd",
            "char", "checksum", "clear", "cls", "collapse", "compress", "confirm", "constraint", "continue",
            "copy", "correlate", "count", "creturn", "datasignature", "describe", "dir", "discard", "display",
            "do", "doedit", "drop", "edit", "egen", "else", "encode", "decode", "end", "erase", "error", "estimates",
            "ereturn", "exit", "expand", "export", "file", "foreach", "format", "forvalues", "frame", "frames",
            "generate", "global", "graph", "gsort", "help", "if", "import", "include", "infile", "infix", "input",
            "insheet", "keep", "label", "list", "local", "log", "logistic", "logit", "macro", "mark", "markout",
            "marksample", "mata", "matrix", "memory", "merge", "mkdir", "more", "net", "noisily", "notes", "numlist",
            "order", "outfile", "outsheet", "plugin", "postfile", "post", "postclose", "predict", "preserve",
            "probit", "program", "python", "query", "quietly", "recast", "regress", "rename", "replace", "reshape",
            "restore", "return", "rmdir", "run", "save", "scalar", "set", "shell", "sleep", "sort", "ssc",
            "summarize", "syntax", "sysuse", "tabulate", "tempfile", "tempname", "tempvar", "timer", "tokenize",
            "translate", "tsset", "type", "update", "use", "version", "webuse", "which", "while", "xtset",
        };
        return commands;
    }

    bool isSingleCommand(const std::string& code)
    {
        std::string line = trim(code);
        if (line.empty() || line.find('\n') != std::string::npos || line.find('\r') != std::string::npos) return false;
        if (isComplete(line) != "complete") return false;
        // comments and #delimit are do-file syntax
        if (line[0] == '*' || line[0] == '#' || line.find("//") != std::string::npos || line.find("/*") != std::string::npos)
        {
            return false;
        }
        std::istringstream words(line);
        std::string first;
        words >> first;
        // exit at Stata's prompt would end Stata; in a do-file it ends the do-file
        return first != "exit";
    }

    namespace
    {
        // The number of bytes of the UTF-8 sequence that starts with `lead`, or 0 if it starts none
        int utf8Length(unsigned char lead)
        {
            if (lead < 0x80) return 1;
            if (lead >= 0xC2 && lead <= 0xDF) return 2;
            if (lead >= 0xE0 && lead <= 0xEF) return 3;
            if (lead >= 0xF0 && lead <= 0xF4) return 4;
            return 0;
        }

        bool isContinuation(unsigned char c)
        {
            return (c & 0xC0) == 0x80;
        }

        void appendLatin1(std::string& out, unsigned char c)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    std::string Utf8Decoder::push(const std::string& bytes)
    {
        std::string in = m_pending + bytes;
        m_pending.clear();
        std::string out;
        out.reserve(in.size());
        std::size_t i = 0;
        while (i < in.size())
        {
            unsigned char c = static_cast<unsigned char>(in[i]);
            int length = utf8Length(c);
            if (length == 1)
            {
                out += in[i++];
                continue;
            }
            if (length == 0)
            {
                appendLatin1(out, c);
                ++i;
                continue;
            }
            // a sequence the chunk ends inside: wait for the rest
            if (i + length > in.size())
            {
                bool valid = true;
                for (std::size_t j = i + 1; j < in.size(); ++j) valid = valid && isContinuation(static_cast<unsigned char>(in[j]));
                if (valid)
                {
                    m_pending = in.substr(i);
                    break;
                }
            }
            bool valid = i + length <= in.size();
            for (int j = 1; valid && j < length; ++j) valid = isContinuation(static_cast<unsigned char>(in[i + j]));
            // overlong 3- and 4-byte forms and surrogates are not UTF-8 either
            if (valid && length == 3)
            {
                unsigned char c1 = static_cast<unsigned char>(in[i + 1]);
                valid = !(c == 0xE0 && c1 < 0xA0) && !(c == 0xED && c1 >= 0xA0);
            }
            if (valid && length == 4)
            {
                unsigned char c1 = static_cast<unsigned char>(in[i + 1]);
                valid = !(c == 0xF0 && c1 < 0x90) && !(c == 0xF4 && c1 >= 0x90);
            }
            if (valid)
            {
                out.append(in, i, length);
                i += length;
            }
            else
            {
                appendLatin1(out, c);
                ++i;
            }
        }
        return out;
    }

    std::string Utf8Decoder::finish()
    {
        std::string out;
        for (char c : m_pending) appendLatin1(out, static_cast<unsigned char>(c));
        m_pending.clear();
        return out;
    }

    namespace
    {
        bool isEchoLine(const std::string& line)
        {
            return line == "." || line.rfind(". ", 0) == 0;
        }

        // "  2.     display `i'": a line of a block or program, numbered as Stata reads it
        bool isNumberedLine(const std::string& line)
        {
            static const std::regex numbered(R"(^\s*\d+\.( .*)?$)");
            return std::regex_match(line, numbered);
        }

        // Whether `partial` could still become a numbered line once its line is complete
        bool mayBeNumbered(const std::string& partial)
        {
            static const std::regex start(R"(^\s*(\d+(\.( .*)?)?)?$)");
            return std::regex_match(partial, start);
        }

        // Whether an echoed command opens lines Stata numbers: a block ({) or a program definition
        bool opensNumberedLines(const std::string& command)
        {
            std::string trimmed = trim(command);
            // a trailing comment does not count: "foreach v of varlist * { // each"
            std::size_t comment = trimmed.find(" //");
            if (comment != std::string::npos) trimmed = trim(trimmed.substr(0, comment));
            if (!trimmed.empty() && trimmed.back() == '{') return true;
            std::istringstream words(trimmed);
            std::string first;
            std::string second;
            words >> first >> second;
            if (first.size() >= 2 && std::string("program").compare(0, first.size(), first) == 0)
            {
                return second != "drop" && second != "dir" && second != "list";
            }
            return false;
        }

        bool isReturnCodeLine(const std::string& line)
        {
            static const std::regex code(R"(^r\(\d+\);$)");
            return std::regex_match(line, code);
        }
    }

    bool EchoFilter::mayBeEcho(const std::string& partial) const
    {
        if (partial.empty()) return true;
        if (partial[0] == '.' && (partial.size() == 1 || partial[1] == ' ')) return true;
        if ((m_afterEcho || (m_numbered && m_continuable)) && partial[0] == '>' && (partial.size() == 1 || partial[1] == ' ')) return true;
        if (m_numbered && mayBeNumbered(partial)) return true;
        // a repeated return code is only known once its line is complete
        if (partial[0] == 'r' && isReturnCodeLine(m_lastLine)) return true;
        return false;
    }

    // One complete line (without its newline): what goes out for it
    std::string EchoFilter::line(const std::string& text)
    {
        std::string line = text;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if ((m_afterEcho || (m_numbered && m_continuable)) && line.rfind("> ", 0) == 0)
        {
            m_command += line.substr(2);
            m_numbered = opensNumberedLines(m_command);
            return std::string();
        }
        if (isEchoLine(line))
        {
            m_blankLines = 0;
            m_afterEcho = true;
            m_command = line.size() > 2 ? line.substr(2) : std::string();
            m_numbered = opensNumberedLines(m_command);
            return std::string();
        }
        m_afterEcho = false;
        if (m_numbered && isNumberedLine(line))
        {
            // a block inside the block: "  3.     if `i' == 2 {" -- the numbering goes on until output; a line too
            // long for c(linesize) goes on in "> " lines
            m_continuable = true;
            return std::string();
        }
        m_continuable = false;
        m_numbered = false;
        if (trim(line).empty())
        {
            ++m_blankLines;
            return std::string();
        }
        // after an error in a do-file Stata prints its return code a second time
        if (isReturnCodeLine(line) && line == m_lastLine && m_blankLines == 0)
        {
            return std::string();
        }
        std::string out(static_cast<std::size_t>(m_blankLines), '\n');
        m_blankLines = 0;
        out += text;
        out += '\n';
        m_lastLine = line;
        return out;
    }

    std::string TailHold::push(const std::string& text)
    {
        m_held += text;
        // keep the last kLines complete lines (and a line not yet complete)
        std::size_t lines = 0;
        std::size_t cut = std::string::npos;
        for (std::size_t i = m_held.size(); i-- > 0;)
        {
            if (m_held[i] == '\n')
            {
                if (lines == kLines)
                {
                    cut = i + 1;
                    break;
                }
                ++lines;
            }
        }
        if (cut == std::string::npos) return std::string();
        std::string out = m_held.substr(0, cut);
        m_held.erase(0, cut);
        return out;
    }

    std::string TailHold::release()
    {
        std::string out;
        out.swap(m_held);
        return out;
    }

    std::string TailHold::finish(const std::string& error)
    {
        std::string out = release();
        if (error.empty()) return out;
        std::size_t end = out.size();
        while (end > 0 && isSpace(out[end - 1])) --end;
        if (end >= error.size() && out.compare(end - error.size(), error.size(), error) == 0)
        {
            // and only at the start of a line
            std::size_t start = end - error.size();
            if (start == 0 || out[start - 1] == '\n')
            {
                out.erase(start);
            }
        }
        return out;
    }

    std::string EchoFilter::push(const std::string& text)
    {
        std::string out;
        std::size_t begin = 0;
        while (begin < text.size())
        {
            std::size_t newline = text.find('\n', begin);
            if (newline == std::string::npos)
            {
                m_partial.append(text, begin, std::string::npos);
                break;
            }
            m_partial.append(text, begin, newline - begin);
            begin = newline + 1;
            if (m_passing)
            {
                out += m_partial;
                out += '\n';
                m_lastLine += m_partial;
                if (!m_lastLine.empty() && m_lastLine.back() == '\r') m_lastLine.pop_back();
                m_passing = false;
            }
            else
            {
                out += line(m_partial);
            }
            m_partial.clear();
        }

        // The start of a line that cannot be echo goes out now (a progress
        // line of dots should not wait for its end).
        if (!m_partial.empty() && (m_passing || !mayBeEcho(m_partial)))
        {
            if (!m_passing)
            {
                out += std::string(static_cast<std::size_t>(m_blankLines), '\n');
                m_blankLines = 0;
                m_afterEcho = false;
                m_numbered = false;
                m_passing = true;
                m_lastLine.clear();
            }
            out += m_partial;
            // the line is kept whole (a repeated return code is compared with it)
            m_lastLine += m_partial;
            m_partial.clear();
        }
        return out;
    }

    std::string EchoFilter::finish()
    {
        std::string out;
        if (m_passing)
        {
            out = m_partial;
        }
        else if (!m_partial.empty() && !isEchoLine(m_partial) && m_partial != ".")
        {
            out = line(m_partial);
            // the line had no newline of its own
            if (!out.empty() && out.back() == '\n') out.pop_back();
        }
        m_partial.clear();
        m_passing = false;
        m_blankLines = 0;
        m_afterEcho = false;
        m_numbered = false;
        m_lastLine.clear();
        return out;
    }

    std::string tokenAt(const std::string& code, int cursorPos)
    {
        int pos = std::clamp(cursorPos, 0, static_cast<int>(code.size()));
        int start = pos;
        int end = pos;
        while (start > 0 && isNameChar(code[start - 1])) --start;
        while (end < static_cast<int>(code.size()) && isNameChar(code[end])) ++end;
        return code.substr(start, end - start);
    }

    std::string errorMessage(const std::string& output, int rc)
    {
        const std::string marker = "r(" + std::to_string(rc) + ");";
        std::vector<std::string> lines = splitLines(output);

        // An error inside a do-file is reported twice -- once where it
        // happened, and once more after "end of do-file" -- so the message
        // belongs to the first r(<rc>); of the trailing run of them.
        int markerLine = -1;
        for (int i = static_cast<int>(lines.size()) - 1; i >= 0; --i)
        {
            std::string line = trim(lines[i]);
            if (line == marker) markerLine = i;
            else if (!line.empty() && line != "end of do-file") break;
        }
        if (markerLine < 0) return std::string();

        int first = markerLine;
        while (first > 0)
        {
            const std::string& line = lines[first - 1];
            // A blank line, or the echoed command (". cmd" / "> continued"),
            // is where the message starts.
            if (trim(line).empty() || line.rfind(". ", 0) == 0 || line.rfind("> ", 0) == 0) break;
            --first;
        }

        std::string message;
        for (int i = first; i < markerLine; ++i)
        {
            if (!message.empty()) message += "\n";
            message += lines[i];
        }
        return trim(message);
    }

} }
