#include <string>
#include <vector>

#include "adrastea/history_manager.hpp"
#include "in_memory_history_manager.hpp"

namespace adrastea
{
    HistoryManager::HistoryManager()
    {
    }

    void HistoryManager::configure()
    {
        configureImpl();
    }

    void HistoryManager::storeInputs(int session,
        int line_num,
        const std::string& input,
        const std::string& output)
    {
        storeInputsImpl(session, line_num, input, output);
    }

    json HistoryManager::processRequest(const json& content) const
    {
        json history;

        std::string hist_access_type = content.value("hist_access_type", "tail");

        if (hist_access_type.compare("tail") == 0)
        {
            int n = content.value("n", 10);
            bool raw = content.value("raw", true);
            bool output = content.value("output", false);

            history = getTail(n, raw, output);
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

            history = getRange(session, start, stop, raw, output);
        }

        return history;
    }

    json HistoryManager::getTail(int n, bool raw, bool output) const
    {
        return getTailImpl(n, raw, output);
    }

    json HistoryManager::getRange(int session, int start, int stop, bool raw, bool output) const
    {
        return getRangeImpl(session, start, stop, raw, output);
    }

    json HistoryManager::search(const std::string& pattern, bool raw, bool output, int n, bool unique) const
    {
        return searchImpl(pattern, raw, output, n, unique);
    }

    std::unique_ptr<HistoryManager> makeInMemoryHistoryManager()
    {
        return std::make_unique<InMemoryHistoryManager>();
    }
}
