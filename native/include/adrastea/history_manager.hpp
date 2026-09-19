#ifndef ADRASTEA_HISTORY_MANAGER_HPP
#define ADRASTEA_HISTORY_MANAGER_HPP

#include <string>
#include <vector>

#include "adrastea.hpp"
#include "json.hpp"

namespace adrastea
{
    class ADRASTEA_API HistoryManager
    {
    public:

        HistoryManager();
        virtual ~HistoryManager() = default;

        HistoryManager(const HistoryManager&) = delete;
        HistoryManager& operator=(const HistoryManager&) = delete;

        HistoryManager(HistoryManager&&) = delete;
        HistoryManager& operator=(HistoryManager&&) = delete;

        void configure();
        void storeInputs(int session,
            int line_num,
            const std::string& input,
            const std::string& output = "");

        json processRequest(const json& content) const;

        json getTail(int n, bool raw, bool output) const;
        json getRange(int session, int start, int stop, bool raw, bool output) const;
        json search(const std::string& pattern, bool raw, bool output, int n, bool unique) const;

    private:

        virtual void configureImpl() = 0;
        virtual void storeInputsImpl(int session,
            int line_num,
            const std::string& input,
            const std::string& output) = 0;

        virtual json getTailImpl(int n, bool raw, bool output) const = 0;
        virtual json getRangeImpl(int session, int start, int stop, bool raw, bool output) const = 0;
        virtual json searchImpl(const std::string& pattern, bool raw, bool output, int n, bool unique) const = 0;
    };

    ADRASTEA_API
    std::unique_ptr<HistoryManager> makeInMemoryHistoryManager();
}

#endif