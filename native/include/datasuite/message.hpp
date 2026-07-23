#ifndef DATASUITE_MESSAGE_HPP
#define DATASUITE_MESSAGE_HPP

#include <string>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp"

namespace datasuite
{
	using binary_buffer = std::vector<char>;
	using buffer_sequence = std::vector<binary_buffer>;

	class DATASUITE_API MessageBase
	{
	public:

		MessageBase(const MessageBase&) = delete;
		MessageBase& operator=(const MessageBase&) = delete;

		const json& header() const { return m_header; }
		const json& parentHeader() const { return m_parentHeader; }
		const json& metadata() const { return m_metadata; }
		const json& content() const { return m_content; }

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

		json m_header;
		json m_parentHeader;
		json m_metadata;
		json m_content;
		buffer_sequence m_buffers;
	};

	class DATASUITE_API Message : public MessageBase
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

	class DATASUITE_API PubMessage : public MessageBase
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

	DATASUITE_API std::string iso8601Now();

	DATASUITE_API std::string_view getProtocolVersion();

	DATASUITE_API json makeHeader(const std::string& msg_type,
									const std::string& user_name,
									const std::string& session_id);
}


#endif
