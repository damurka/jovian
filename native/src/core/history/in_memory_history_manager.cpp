#include <iterator>
#include <stdexcept>
#include <string>
#include <regex>

#include "datasuite/json.hpp"
#include "in_memory_history_manager.hpp"

namespace datasuite
{
    InMemoryHistoryManager::InMemoryHistoryManager()
    {
    }

    InMemoryHistoryManager::~InMemoryHistoryManager()
    {
    }

    void InMemoryHistoryManager::configureImpl()
    {
    }

    void InMemoryHistoryManager::storeInputsImpl(int session,
        int line_num,
        const std::string& input,
        const std::string& output)
    {
        m_history.push_back({ session, line_num, { input, output } });
    }

    InMemoryHistoryManager::short_entry makeShortEntry(const InMemoryHistoryManager::entry& in)
    {
        InMemoryHistoryManager::short_entry res = { std::get<0>(in), std::get<1>(in), std::get<2>(in).first };
        return res;
    }

    json InMemoryHistoryManager::getTailImpl(int n, bool /*raw*/, bool output) const
    {
        json reply;

        int count = std::min(n, static_cast<int>(m_history.size()));

        if (output)
        {
            history_type history;
            std::copy(m_history.rbegin(),
                std::next(m_history.rbegin(), count),
                std::front_inserter(history));

            reply["history"] = history;
        }
        else
        {
            short_history_type history;
            std::transform(m_history.rbegin(),
                std::next(m_history.rbegin(), count),
                std::front_inserter(history),
                makeShortEntry);
            reply["history"] = history;
        }

        reply["status"] = "ok";
        return reply;
    }

    json InMemoryHistoryManager::getRangeImpl(int /*session*/,
        int start,
        int stop,
        bool /*raw*/,
        bool output) const
    {
        json reply;

        int hist_size = static_cast<int>(m_history.size());
        if (start > stop || start > hist_size)
        {
            reply["status"] = "error";
            reply["ename"] = "history_request_error";
            reply["evalue"] = "get_range: start is too high given stop or current history";

            return reply;
        }

        int count = std::min(stop, hist_size) - start;

        if (output)
        {
            history_type history;
            std::copy_n(std::next(m_history.cbegin(), start), count, std::back_inserter(history));
            reply["history"] = history;
        }
        else
        {
            short_history_type history;
            std::transform(std::next(m_history.cbegin(), start),
                std::next(m_history.cbegin(), start + count),
                std::back_inserter(history),
                makeShortEntry);
            reply["history"] = history;
        }


        reply["status"] = "ok";

        return reply;
    }

    template <class InputIt, class OutputIt, class Predicate, class Operation>
    OutputIt transformIf(InputIt first, InputIt last, OutputIt d_first,
        Predicate pred, Operation op)
    {
        while (first != last)
        {
            if (pred(*first))
            {
                *d_first++ = op(*first);
            }
            ++first;
        }
        return d_first;
    }

    template <class H>
    void cleanHistory(H& history, int n, bool unique)
    {
        if (unique)
        {
            auto last = std::unique(history.begin(), history.end());
            history.erase(last, history.end());
        }

        int nb_erase = static_cast<int>(history.size()) - n;
        if (nb_erase > 0)
        {
            history.erase(history.begin(), std::next(history.begin(), nb_erase));
        }
    }

    json InMemoryHistoryManager::searchImpl(const std::string& pattern,
        bool /*raw*/,
        bool output,
        int n,
        bool unique) const
    {
        json reply;

        // Sanitize the pattern from special regex characters
        std::regex special_chars(R"([-[\]{}()+.,\^$|#\s])");
        std::string sanitized = std::regex_replace(pattern, special_chars, R"(\$&)");

        // Turn the glob pattern into regex (simple version)
        std::string regex_pattern = std::regex_replace(std::regex_replace(sanitized, std::regex("\\?"), "."), std::regex("\\*"), ".*");

        std::regex regex(regex_pattern);
        std::cmatch m;

        auto regex_lambda = [&m, &regex](const auto& item) { return std::regex_search(std::get<2>(item).first.c_str(), m, regex); };
        if (output)
        {
            history_type history;
            std::copy_if(m_history.cbegin(), m_history.cend(), std::back_inserter(history), regex_lambda);
            cleanHistory(history, n, unique);
            reply["history"] = history;
        }
        else
        {
            short_history_type history;
            transformIf(m_history.cbegin(),
                m_history.cend(),
                std::back_inserter(history),
                regex_lambda,
                makeShortEntry);
            cleanHistory(history, n, unique);
            reply["history"] = history;
        }

        reply["status"] = "ok";

        return reply;
    }
}
