#include "nlohmann/json.hpp"
#include "datasuite/comm.hpp"
#include "core/kernel/kernel_core.hpp"

namespace nl = nlohmann;

namespace datasuite
{
    /*******************************
     * comm_target implementation *
     *******************************/

    comm_target::comm_target(const std::string& name,
        const function_type& callback,
        comm_manager* manager)
        : m_name(name)
        , m_callback(callback)
        , p_manager(manager)
    {
    }

    const std::string& comm_target::name() const noexcept
    {
        return m_name;
    }

    void comm_target::operator()(comm&& c, message request) const
    {
        return m_callback(std::move(c), std::move(request));
    }

    void comm_target::register_comm(guid id, comm* c) const
    {
        p_manager->register_comm(id, c);
    }

    void comm_target::unregister_comm(guid id) const
    {
        p_manager->unregister_comm(id);
    }

    void comm_target::publish_message(const std::string& msg_type,
        nl::json metadata,
        nl::json content,
        buffer_sequence buffers) const
    {
        if (p_manager->p_kernel != nullptr)
        {
            p_manager->p_kernel->publish_message(
                msg_type, p_manager->p_kernel->parent_header(),
                std::move(metadata), std::move(content),
                std::move(buffers), channel::SHELL
            );
        }
    }

    /************************
     * comm implementation *
     ************************/

    const comm_target& comm::target() const noexcept
    {
        return *p_target;
    }

    void comm::handle_close(message request)
    {
        if (m_close_handler)
        {
            m_close_handler(std::move(request));
        }
    }

    void comm::handle_message(message request)
    {
        if (m_message_handler)
        {
            m_message_handler(std::move(request));
        }
    }

    void comm::send_comm_message(const std::string& msg_type,
        nl::json metadata,
        nl::json data,
        buffer_sequence buffers) const
    {
        nl::json content;
        content["comm_id"] = m_id;
        content["data"] = std::move(data);
        target().publish_message(msg_type, std::move(metadata), std::move(content), std::move(buffers));
    }

    void comm::send_comm_message(const std::string& msg_type,
        nl::json metadata,
        nl::json data,
        buffer_sequence buffers,
        const std::string& target_name) const
    {
        nl::json content;
        content["comm_id"] = m_id;
        content["target_name"] = target_name;
        content["data"] = std::move(data);
        target().publish_message(msg_type, std::move(metadata), std::move(content), std::move(buffers));
    }

    comm::comm(comm&& other)
        : m_close_handler(std::move(other.m_close_handler))
        , m_message_handler(std::move(other.m_message_handler))
        , p_target(std::move(other.p_target))
        , m_id(std::move(other.m_id))
        , m_moved_from(false)
    {
        other.m_moved_from = true;
        p_target->register_comm(m_id, this);
    }

    comm::comm(const comm& other)
        : p_target(other.p_target)
        , m_id(new_guid())
        , m_moved_from(false)
    {
        p_target->register_comm(m_id, this);
    }

    comm& comm::operator=(comm&& other)
    {
        m_close_handler = std::move(other.m_close_handler);
        m_message_handler = std::move(other.m_message_handler);
        p_target = std::move(other.p_target);
        p_target->unregister_comm(m_id);
        m_id = std::move(other.m_id);
        m_moved_from = false;
        other.m_moved_from = true;
        p_target->register_comm(m_id, this);
        return *this;
    }

    comm& comm::operator=(const comm& other)
    {
        p_target = other.p_target;
        p_target->unregister_comm(m_id);
        m_id = new_guid();
        m_moved_from = false;
        p_target->register_comm(m_id, this);
        return *this;
    }

    comm::comm(const comm_target* target, guid id)
        : p_target(target)
        , m_id(id)
    {
        if (!p_target)
            throw std::runtime_error("Cannot initialize comm with null target");
        p_target->register_comm(m_id, this);
    }

    comm::~comm()
    {
        if (!m_moved_from)
        {
            p_target->unregister_comm(m_id);
        }
    }

    void comm::open(nl::json metadata, nl::json data, buffer_sequence buffers)
    {
        send_comm_message("comm_open", std::move(metadata), std::move(data), std::move(buffers), p_target->name());
    }

    void comm::close(nl::json metadata, nl::json data, buffer_sequence buffers)
    {
        send_comm_message("comm_close", std::move(metadata), std::move(data), std::move(buffers));
    }

    void comm::send(nl::json metadata, nl::json data, buffer_sequence buffers) const
    {
        send_comm_message("comm_msg", std::move(metadata), std::move(data), std::move(buffers));
    }

    guid comm::id() const noexcept
    {
        return m_id;
    }

    /********************************
     * comm_manager implementation *
     ********************************/

    comm_manager::comm_manager(kernel_core* kernel)
    {
        p_kernel = kernel;
    }

    nl::json comm_manager::get_metadata() const
    {
        nl::json metadata;
        metadata["started"] = iso8601_now();
        return metadata;
    }

    void comm_manager::register_comm_target(const std::string& target_name,
        const target_function_type& callback)
    {
        m_targets.insert_or_assign(target_name, comm_target(target_name, callback, this));
    }

    void comm_manager::unregister_comm_target(const std::string& target_name)
    {
        m_targets.erase(target_name);
    }

    void comm_manager::register_comm(guid id, comm* c)
    {
        m_comms[id] = c;
    }

    void comm_manager::unregister_comm(guid id)
    {
        m_comms.erase(id);
    }

    void comm_manager::comm_open(message request)
    {
        const nl::json& content = request.content();
        std::string target_name = content["target_name"];
        auto position = m_targets.find(target_name);

        if (position == m_targets.end())
        {
            if (p_kernel != nullptr)
            {
                p_kernel->publish_message(
                    "comm_close", request.header(), nl::json::object(), content, buffer_sequence(), channel::SHELL
                );
            }
        }
        else
        {
            comm_target& trg = position->second;
            guid id = content["comm_id"];
            comm new_comm(&trg, id);
            trg(std::move(new_comm), std::move(request));
        }
    }

    void comm_manager::comm_close(message request)
    {
        const nl::json& content = request.content();
        guid id = content["comm_id"];
        auto position = m_comms.find(id);
        if (position == m_comms.end())
        {
            throw std::runtime_error("No such comm registered: " + std::string(id.data()));
        }
        else
        {
            position->second->handle_close(std::move(request));
        }
        m_comms.erase(id);
    }

    void comm_manager::comm_msg(message request)
    {
        const nl::json& content = request.content();
        guid id = content["comm_id"];
        auto position = m_comms.find(id);
        if (position == m_comms.end())
        {
            throw std::runtime_error("No such comm registered: " + std::string(id.data()));
        }
        else
        {
            position->second->handle_message(std::move(request));
        }
    }

    const comm_target* comm_manager::target(const std::string& target_name) const
    {
        auto iter = m_targets.find(target_name);
        return iter == m_targets.end() ? nullptr : &(iter->second);
    }

    const std::map<guid, comm*>& comm_manager::comms() const noexcept
    {
        return m_comms;
    }
}
