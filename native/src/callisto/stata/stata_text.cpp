#include "callisto/stata/stata_text.hpp"

#include <algorithm>
#include <cctype>
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
        if (before == '`') token.kind = CompletionKind::Local;
        else if (before == '$' || (before == '{' && beforeThat == '$')) token.kind = CompletionKind::Global;
        else if (!token.prefix.empty() && !std::isdigit(static_cast<unsigned char>(token.prefix[0])))
            token.kind = CompletionKind::Variable;
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
