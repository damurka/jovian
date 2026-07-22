#ifndef DATASUITE_ZMQ_SERIALIZER_HPP
#define DATASUITE_ZMQ_SERIALIZER_HPP

#include "zmq_addon.hpp"

#include "datasuite/message.hpp"

#include "authentication.hpp"

namespace datasuite
{
    class ZmqSerializer
    {
    public:

        static zmq::multipart_t serialize(Message&& msg,
            const Authentication& auth,
            json::error_handler_t error_handler = json::error_handler_t::strict);

        static Message deserialize(zmq::multipart_t& wire_msg,
            const Authentication& auth);

        static zmq::multipart_t serialize_iopub(PubMessage&& msg,
            const Authentication& auth,
            json::error_handler_t error_handler = json::error_handler_t::strict);

        static PubMessage deserialize_iopub(zmq::multipart_t& wire_msg,
            const Authentication& auth);


        static void serialize_zmq_id(const Message::guid_list& ids, zmq::multipart_t& wire_msg);
        static Message::guid_list deserialize_zmq_id(zmq::multipart_t& wire_msg);

        static RawBuffer make_raw_buffer(zmq::message_t& msg);
    };

}

#endif
