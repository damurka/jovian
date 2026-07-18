#ifndef DATASUITE_HISTORY_MANAGER_HPP
#define DATASUITE_HISTORY_MANAGER_HPP

#include <string>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    class DATASUITE_API history_manager
    {
    public:

        history_manager();
        virtual ~history_manager() = default;

        history_manager(const history_manager&) = delete;
        history_manager& operator=(const history_manager&) = delete;

        history_manager(history_manager&&) = delete;
        history_manager& operator=(history_manager&&) = delete;

        void configure();
        void store_inputs(int session,
            int line_num,
            const std::string& input,
            const std::string& output = "");

        nl::json process_request(const nl::json& content) const;

        nl::json get_tail(int n, bool raw, bool output) const;
        nl::json get_range(int session, int start, int stop, bool raw, bool output) const;
        nl::json search(const std::string& pattern, bool raw, bool output, int n, bool unique) const;

    private:

        virtual void configure_impl() = 0;
        virtual void store_inputs_impl(int session,
            int line_num,
            const std::string& input,
            const std::string& output) = 0;

        virtual nl::json get_tail_impl(int n, bool raw, bool output) const = 0;
        virtual nl::json get_range_impl(int session, int start, int stop, bool raw, bool output) const = 0;
        virtual nl::json search_impl(const std::string& pattern, bool raw, bool output, int n, bool unique) const = 0;
    };

    DATASUITE_API
    std::unique_ptr<history_manager> make_in_memory_history_manager();
}

#endif