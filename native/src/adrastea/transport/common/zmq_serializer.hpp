#ifndef ADRASTEA_ZMQ_SERIALIZER_HPP
#define ADRASTEA_ZMQ_SERIALIZER_HPP

#include "zmq_addon.hpp"

#include "adrastea/message.hpp"

#include "authentication.hpp"

namespace adrastea
{
    class ZmqSerializer
    {
    public:

        static zmq::multipart_t serialize(Message&& msg,
            const Authentication& auth,
            json::error_handler_t error_handler = json::error_handler_t::strict);

        // defer_large_content: a content frame of kDeferredContentBytes or more is kept as its text and parsed
        // only if something reads content() (Message::contentText()), provided it is one line of valid UTF-8.
        // For a supervisor, which passes a kernel's messages on and has no use for what a large one holds.
        static Message deserialize(zmq::multipart_t& wire_msg,
            const Authentication& auth,
            bool defer_large_content = false);

        static zmq::multipart_t serializeIopub(PubMessage&& msg,
            const Authentication& auth,
            json::error_handler_t error_handler = json::error_handler_t::strict);

        static PubMessage deserializeIopub(zmq::multipart_t& wire_msg,
            const Authentication& auth,
            bool defer_large_content = false);

        static constexpr std::size_t kDeferredContentBytes = 64 * 1024;


        static void serializeZmqId(const Message::guid_list& ids, zmq::multipart_t& wire_msg);
        static Message::guid_list deserializeZmqId(zmq::multipart_t& wire_msg);

        static RawBuffer makeRawBuffer(zmq::message_t& msg);
    };

}

#endif
