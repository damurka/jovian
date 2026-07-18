#ifndef DATASUITE_ZMQ_SERIALIZER_HPP
#define DATASUITE_ZMQ_SERIALIZER_HPP

#include "zmq_addon.hpp"

#include "datasuite/message.hpp"

#include "authentication.hpp"

namespace datasuite
{
    class zmq_serializer
    {
    public:

        static zmq::multipart_t serialize(message&& msg,
            const authentication& auth,
            nl::json::error_handler_t error_handler = nl::json::error_handler_t::strict);

        static message deserialize(zmq::multipart_t& wire_msg,
            const authentication& auth);

        static zmq::multipart_t serialize_iopub(pub_message&& msg,
            const authentication& auth,
            nl::json::error_handler_t error_handler = nl::json::error_handler_t::strict);

        static pub_message deserialize_iopub(zmq::multipart_t& wire_msg,
            const authentication& auth);


        static void serialize_zmq_id(const message::guid_list& ids, zmq::multipart_t& wire_msg);
        static message::guid_list deserialize_zmq_id(zmq::multipart_t& wire_msg);

        static raw_buffer make_raw_buffer(zmq::message_t& msg);
    };

}

#endif
