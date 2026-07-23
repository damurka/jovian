#ifndef DATASUITE_IN_MEMORY_HISTORY_MANAGER_HPP
#define DATASUITE_IN_MEMORY_HISTORY_MANAGER_HPP

#include <array>
#include <list>
#include <string>
#include <utility>

#include "datasuite/json.hpp"
#include "datasuite/datasuite.hpp"
#include "datasuite/history_manager.hpp"

namespace datasuite
{
    class InMemoryHistoryManager : public HistoryManager
    {
    public:

        using string_pair = std::pair<std::string, std::string>;
        using entry = std::tuple<int, int, string_pair>;
        using history_type = std::list<entry>;
        using short_entry = std::tuple<int, int, std::string>;
        using short_history_type = std::list<short_entry>;

        InMemoryHistoryManager();
        virtual ~InMemoryHistoryManager();

    private:

        void configureImpl() override;
        void storeInputsImpl(int session,
            int line_num,
            const std::string& input,
            const std::string& output) override;

        json getTailImpl(int n, bool raw, bool output) const override;
        json getRangeImpl(int session, int start, int stop, bool raw, bool output) const override;
        json searchImpl(const std::string& pattern, bool raw, bool output, int n, bool unique) const override;

        history_type m_history;
    };
}

#endif
