#ifndef DATASUITE_HISTORY_MANAGER_HPP
#define DATASUITE_HISTORY_MANAGER_HPP

#include <string>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
    class DATASUITE_API HistoryManager
    {
    public:

        HistoryManager();
        virtual ~HistoryManager() = default;

        HistoryManager(const HistoryManager&) = delete;
        HistoryManager& operator=(const HistoryManager&) = delete;

        HistoryManager(HistoryManager&&) = delete;
        HistoryManager& operator=(HistoryManager&&) = delete;

        void configure();
        void store_inputs(int session,
            int line_num,
            const std::string& input,
            const std::string& output = "");

        json process_request(const json& content) const;

        json get_tail(int n, bool raw, bool output) const;
        json get_range(int session, int start, int stop, bool raw, bool output) const;
        json search(const std::string& pattern, bool raw, bool output, int n, bool unique) const;

    private:

        virtual void configure_impl() = 0;
        virtual void store_inputs_impl(int session,
            int line_num,
            const std::string& input,
            const std::string& output) = 0;

        virtual json get_tail_impl(int n, bool raw, bool output) const = 0;
        virtual json get_range_impl(int session, int start, int stop, bool raw, bool output) const = 0;
        virtual json search_impl(const std::string& pattern, bool raw, bool output, int n, bool unique) const = 0;
    };

    DATASUITE_API
    std::unique_ptr<HistoryManager> make_in_memory_history_manager();
}

#endif