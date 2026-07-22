#include <string>
#include <vector>

#include "datasuite/history_manager.hpp"
#include "in_memory_history_manager.hpp"

namespace datasuite
{
    HistoryManager::HistoryManager()
    {
    }

    void HistoryManager::configure()
    {
        configure_impl();
    }

    void HistoryManager::store_inputs(int session,
        int line_num,
        const std::string& input,
        const std::string& output)
    {
        store_inputs_impl(session, line_num, input, output);
    }

    json HistoryManager::process_request(const json& content) const
    {
        json history;

        std::string hist_access_type = content.value("hist_access_type", "tail");

        if (hist_access_type.compare("tail") == 0)
        {
            int n = content.value("n", 10);
            bool raw = content.value("raw", true);
            bool output = content.value("output", false);

            history = get_tail(n, raw, output);
        }

        if (hist_access_type.compare("search") == 0)
        {
            std::string pattern = content.value("pattern", "*");
            bool raw = content.value("raw", true);
            bool output = content.value("output", false);
            int n = content.value("n", 10);
            bool unique = content.value("unique", false);

            history = search(pattern, raw, output, n, unique);
        }

        if (hist_access_type.compare("range") == 0)
        {
            int session = content.value("session", 0);
            int start = content.value("start", 1);
            int stop = content.value("stop", 10);
            bool raw = content.value("raw", true);
            bool output = content.value("output", false);

            history = get_range(session, start, stop, raw, output);
        }

        return history;
    }

    json HistoryManager::get_tail(int n, bool raw, bool output) const
    {
        return get_tail_impl(n, raw, output);
    }

    json HistoryManager::get_range(int session, int start, int stop, bool raw, bool output) const
    {
        return get_range_impl(session, start, stop, raw, output);
    }

    json HistoryManager::search(const std::string& pattern, bool raw, bool output, int n, bool unique) const
    {
        return search_impl(pattern, raw, output, n, unique);
    }

    std::unique_ptr<HistoryManager> make_in_memory_history_manager()
    {
        return std::make_unique<InMemoryHistoryManager>();
    }
}
