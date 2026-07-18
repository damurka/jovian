#include "datasuite/request_context.hpp"

namespace datasuite
{
    request_context::request_context(nl::json header, guid_list id)
        : m_header(std::move(header)), m_id(std::move(id))
    {
    }

    const nl::json& request_context::header() const
    {
        return m_header;
    }

    const message::guid_list& request_context::id() const
    {
        return m_id;
    }
}
