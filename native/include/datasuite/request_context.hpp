#ifndef DATASUITE_REQUEST_CONTEXT_HPP
#define DATASUITE_REQUEST_CONTEXT_HPP

#include <string>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp" 
#include "message.hpp" // for message::guid_list

namespace nl = nlohmann;

namespace datasuite
{
    class DATASUITE_API request_context
    {
    public:

        using guid_list = message::guid_list;

        request_context() = default;
        request_context(nl::json header, guid_list id);

        const nl::json& header() const;
        const guid_list& id() const;

    private:

        nl::json m_header = nl::json::object();
        guid_list m_id;
    };
}

#endif