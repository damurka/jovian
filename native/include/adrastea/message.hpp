#ifndef ADRASTEA_MESSAGE_HPP
#define ADRASTEA_MESSAGE_HPP

#include <string>
#include <vector>

#include "adrastea.hpp"
#include "json.hpp"

namespace adrastea
{
	using binary_buffer = std::vector<char>;
	using buffer_sequence = std::vector<binary_buffer>;

	class ADRASTEA_API MessageBase
	{
	public:

		MessageBase(const MessageBase&) = delete;
		MessageBase& operator=(const MessageBase&) = delete;

		const json& header() const { return m_header; }
		const json& parentHeader() const { return m_parentHeader; }
		const json& metadata() const { return m_metadata; }
		// Parsed on first use when the message was given its content as text (deferContent()).
		const json& content() const { if (m_contentDeferred) { parseDeferredContent(); } return m_content; }

		// The content's JSON text exactly as it was received, or nullptr when the message was not given it. The
		// text is on one line and valid UTF-8 (deferContent()'s caller has checked), so it can be put inside
		// another JSON text as it is: how a supervisor passes a large message on without parsing 10 MB to write
		// the same 10 MB again.
		const std::string* contentText() const { return m_hasContentText ? &m_contentText : nullptr; }

		// The content as text, parsed only if something reads content(). For a caller that has checked the text
		// is one line of valid UTF-8; what it holds beyond that is the sender's (the kernel signed it).
		void deferContent(std::string text);

		const buffer_sequence& buffers() const& { return m_buffers; }
		buffer_sequence&& buffers()&& { return std::move(m_buffers); }

	protected:
		MessageBase() = default;
		MessageBase(json header,
					json parent_header,
					json metadata,
					json content,
					buffer_sequence buffers);
		~MessageBase() = default;

		MessageBase(MessageBase&&) = default;
		MessageBase& operator=(MessageBase&&) = default;

	private:

		void parseDeferredContent() const;

		json m_header;
		json m_parentHeader;
		json m_metadata;
		mutable json m_content;
		mutable bool m_contentDeferred = false;
		bool m_hasContentText = false;
		std::string m_contentText;
		buffer_sequence m_buffers;
	};

	class ADRASTEA_API Message : public MessageBase
	{
	public:
		using guid_list = std::vector<std::string>;

		Message() = default;
		Message(const guid_list& zmq_id,
				json header,
				json parent_header,
				json metadata,
				json content,
				buffer_sequence buffers);

		~Message() = default;

		Message(Message&&) = default;
		Message& operator=(Message&&) = default;

		Message(const Message&) = delete;
		Message& operator=(const Message&) = delete;

		const guid_list& identities() const { return m_zmqId; }

	private:
		guid_list m_zmqId;
	};

	class ADRASTEA_API PubMessage : public MessageBase
	{
	public:

		using base_type = MessageBase;

		PubMessage() = default;
		PubMessage(const std::string& topic,
					json header,
					json parent_header,
					json metadata,
					json content,
					buffer_sequence buffers);

		~PubMessage() = default;

		PubMessage(PubMessage&&) = default;
		PubMessage& operator=(PubMessage&&) = default;

		PubMessage(const PubMessage&) = delete;
		PubMessage& operator=(const PubMessage&) = delete;

		const std::string& topic() const { return m_topic; }

	private:

		std::string m_topic;
	};

	ADRASTEA_API std::string iso8601Now();

	ADRASTEA_API std::string_view getProtocolVersion();

	ADRASTEA_API json makeHeader(const std::string& msg_type,
									const std::string& user_name,
									const std::string& session_id);
}


#endif
