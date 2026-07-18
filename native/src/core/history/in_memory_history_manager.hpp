#ifndef DATASUITE_IN_MEMORY_HISTORY_MANAGER_HPP
#define DATASUITE_IN_MEMORY_HISTORY_MANAGER_HPP

#include <array>
#include <list>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"

#include "datasuite/datasuite.hpp"
#include "datasuite/history_manager.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    class in_memory_history_manager : public history_manager
    {
    public:

        using string_pair = std::pair<std::string, std::string>;
        using entry = std::tuple<int, int, string_pair>;
        using history_type = std::list<entry>;
        using short_entry = std::tuple<int, int, std::string>;
        using short_history_type = std::list<short_entry>;

        in_memory_history_manager();
        virtual ~in_memory_history_manager();

    private:

        void configure_impl() override;
        void store_inputs_impl(int session,
            int line_num,
            const std::string& input,
            const std::string& output) override;

        nl::json get_tail_impl(int n, bool raw, bool output) const override;
        nl::json get_range_impl(int session, int start, int stop, bool raw, bool output) const override;
        nl::json search_impl(const std::string& pattern, bool raw, bool output, int n, bool unique) const override;

        history_type m_history;
    };
}

#endif
