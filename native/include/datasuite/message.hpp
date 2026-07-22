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

	class DATASUITE_API message_base
	{
	public:

		message_base(const message_base&) = delete;
		message_base& operator=(const message_base&) = delete;

		const json& header() const { return m_header; }
		const json& parent_header() const { return m_parentHeader; }
		const json& metadata() const { return m_metadata; }
		const json& content() const { return m_content; }

		const buffer_sequence& buffers() const& { return m_buffers; }
		buffer_sequence&& buffers()&& { return std::move(m_buffers); }

	protected:
		message_base() = default;
		message_base(json header,
					json parent_header,
					json metadata,
					json content,
					buffer_sequence buffers);
		~message_base() = default;

		message_base(message_base&&) = default;
		message_base& operator=(message_base&&) = default;

	private:

		json m_header;
		json m_parentHeader;
		json m_metadata;
		json m_content;
		buffer_sequence m_buffers;
	};

	class DATASUITE_API message : public message_base
	{
	public:
		using guid_list = std::vector<std::string>;

		message() = default;
		message(const guid_list& zmq_id,
				json header,
				json parent_header,
				json metadata,
				json content,
				buffer_sequence buffers);

		~message() = default;

		message(message&&) = default;
		message& operator=(message&&) = default;

		message(const message&) = delete;
		message& operator=(const message&) = delete;

		const guid_list& identities() const { return m_zmqId; }

	private:
		guid_list m_zmqId;
	};

	class DATASUITE_API pub_message : public message_base
	{
	public:

		using base_type = message_base;

		pub_message() = default;
		pub_message(const std::string& topic,
					json header,
					json parent_header,
					json metadata,
					json content,
					buffer_sequence buffers);

		~pub_message() = default;

		pub_message(pub_message&&) = default;
		pub_message& operator=(pub_message&&) = default;

		pub_message(const pub_message&) = delete;
		pub_message& operator=(const pub_message&) = delete;

		const std::string& topic() const { return m_topic; }

	private:

		std::string m_topic;
	};

	DATASUITE_API std::string iso8601_now();

	DATASUITE_API std::string_view get_protocol_version();

	DATASUITE_API json make_header(const std::string& msg_type,
									const std::string& user_name,
									const std::string& session_id);
}


#endif