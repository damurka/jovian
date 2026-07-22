#ifndef DATASUITE_REQUEST_CONTEXT_HPP
#define DATASUITE_REQUEST_CONTEXT_HPP

#include <string>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp" 
#include "message.hpp" // for message::guid_list

namespace datasuite
{
    class DATASUITE_API RequestContext
    {
    public:

        using guid_list = message::guid_list;

        RequestContext() = default;
        RequestContext(json header, guid_list id);

        const json& header() const;
        const guid_list& id() const;

    private:

        json m_header = json::object();
        guid_list m_id;
    };
}

#endif