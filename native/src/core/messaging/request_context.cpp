#include "datasuite/json.hpp"
#include "datasuite/request_context.hpp"

namespace datasuite
{
    RequestContext::RequestContext(json header, guid_list id)
        : m_header(std::move(header)), m_id(std::move(id))
    {
    }

    const json& RequestContext::header() const
    {
        return m_header;
    }

    const message::guid_list& RequestContext::id() const
    {
        return m_id;
    }
}
