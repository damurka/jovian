#ifndef DATASUITE_MESSAGE_HPP
#define DATASUITE_MESSAGE_HPP

#include <string>
#include <vector>

#include "datasuite.hpp"
#include "json.hpp"

namespace nl = nlohmann;

namespace datasuite
{
	using binary_buffer = std::vector<char>;
	using buffer_sequence = std::vector<binary_buffer>;

	struct DATASUITE_API message_base_data
	{
		nl::json m_header;
		nl::json m_parent_header;
		nl::json m_metadata;
		nl::json m_content;
		buffer_sequence m_buffers;
	};

	class DATASUITE_API message_base
	{
	public:

		message_base(const message_base&) = delete;
		message_base& operator=(const message_base&) = delete;

		const nl::json& header() const;
		const nl::json& parent_header() const;
		const nl::json& metadata() const;
		const nl::json& content() const;

		const buffer_sequence& buffers() const&;
		buffer_sequence&& buffers()&&;

	protected:
		message_base() = default;
		message_base(nl::json header,
			nl::json parent_header,
			nl::json metadata,
			nl::json content,
			buffer_sequence buffers);
		message_base(message_base_data&& data);
		~message_base() = default;

		message_base(message_base&&) = default;
		message_base& operator=(message_base&&) = default;

	private:

		nl::json m_header;
		nl::json m_parent_header;
		nl::json m_metadata;
		nl::json m_content;
		buffer_sequence m_buffers;
	};

	class DATASUITE_API message : public message_base
	{
	public:
		using base_type = message_base;
		using guid_list = std::vector<std::string>;

		message() = default;
		message(const guid_list& zmq_id,
			nl::json header,
			nl::json parent_header,
			nl::json metadata,
			nl::json content,
			buffer_sequence buffers);
		message(const guid_list& zmq_id,
			message_base_data&& data);

		~message() = default;

		message(message&&) = default;
		message& operator=(message&&) = default;

		message(const message&) = delete;
		message& operator=(const message&) = delete;

		const guid_list& identities() const;

	private:
		guid_list m_zmq_id;
	};

	class DATASUITE_API pub_message : public message_base
	{
	public:

		using base_type = message_base;

		pub_message() = default;
		pub_message(const std::string& topic,
			nl::json header,
			nl::json parent_header,
			nl::json metadata,
			nl::json content,
			buffer_sequence buffers);
		pub_message(const std::string& topic,
			message_base_data&& data);

		~pub_message() = default;

		pub_message(pub_message&&) = default;
		pub_message& operator=(pub_message&&) = default;

		pub_message(const pub_message&) = delete;
		pub_message& operator=(const pub_message&) = delete;

		const std::string& topic() const;

	private:

		std::string m_topic;
	};

	DATASUITE_API std::string iso8601_now();

	DATASUITE_API std::string_view get_protocol_version();

	DATASUITE_API nl::json make_header(const std::string& msg_type,
		const std::string& user_name,
		const std::string& session_id);
}


#endif