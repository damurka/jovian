#include "datasuite/json.hpp"
#include "datasuite/comm.hpp"
#include "core/kernel/kernel_core.hpp"

namespace datasuite
{
    /*******************************
     * CommTarget implementation *
     *******************************/

    CommTarget::CommTarget(const std::string& name,
        const function_type& callback,
        CommManager* manager)
        : m_name(name)
        , m_callback(callback)
        , p_manager(manager)
    {
    }

    const std::string& CommTarget::name() const noexcept
    {
        return m_name;
    }

    void CommTarget::operator()(Comm&& c, Message request) const
    {
        return m_callback(std::move(c), std::move(request));
    }

    void CommTarget::register_comm(Guid id, Comm* c) const
    {
        p_manager->register_comm(id, c);
    }

    void CommTarget::unregister_comm(Guid id) const
    {
        p_manager->unregister_comm(id);
    }

    void CommTarget::publish_message(const std::string& msg_type,
        json metadata,
        json content,
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
     * Comm implementation *
     ************************/

    const CommTarget& Comm::target() const noexcept
    {
        return *p_target;
    }

    void Comm::handle_close(Message request)
    {
        if (m_closeHandler)
        {
            m_closeHandler(std::move(request));
        }
    }

    void Comm::handle_message(Message request)
    {
        if (m_messageHandler)
        {
            m_messageHandler(std::move(request));
        }
    }

    void Comm::send_comm_message(const std::string& msg_type,
        json metadata,
        json data,
        buffer_sequence buffers) const
    {
        json content;
        content["comm_id"] = m_id;
        content["data"] = std::move(data);
        target().publish_message(msg_type, std::move(metadata), std::move(content), std::move(buffers));
    }

    void Comm::send_comm_message(const std::string& msg_type,
        json metadata,
        json data,
        buffer_sequence buffers,
        const std::string& target_name) const
    {
        json content;
        content["comm_id"] = m_id;
        content["target_name"] = target_name;
        content["data"] = std::move(data);
        target().publish_message(msg_type, std::move(metadata), std::move(content), std::move(buffers));
    }

    Comm::Comm(Comm&& other)
        : m_closeHandler(std::move(other.m_closeHandler))
        , m_messageHandler(std::move(other.m_messageHandler))
        , p_target(std::move(other.p_target))
        , m_id(std::move(other.m_id))
        , m_movedFrom(false)
    {
        other.m_movedFrom = true;
        p_target->register_comm(m_id, this);
    }

    Comm::Comm(const Comm& other)
        : p_target(other.p_target)
        , m_id(new_guid())
        , m_movedFrom(false)
    {
        p_target->register_comm(m_id, this);
    }

    Comm& Comm::operator=(Comm&& other)
    {
        m_closeHandler = std::move(other.m_closeHandler);
        m_messageHandler = std::move(other.m_messageHandler);
        p_target = std::move(other.p_target);
        p_target->unregister_comm(m_id);
        m_id = std::move(other.m_id);
        m_movedFrom = false;
        other.m_movedFrom = true;
        p_target->register_comm(m_id, this);
        return *this;
    }

    Comm& Comm::operator=(const Comm& other)
    {
        p_target = other.p_target;
        p_target->unregister_comm(m_id);
        m_id = new_guid();
        m_movedFrom = false;
        p_target->register_comm(m_id, this);
        return *this;
    }

    Comm::Comm(const CommTarget* target, Guid id)
        : p_target(target)
        , m_id(id)
    {
        if (!p_target)
            throw std::runtime_error("Cannot initialize comm with null target");
        p_target->register_comm(m_id, this);
    }

    Comm::~Comm()
    {
        if (!m_movedFrom)
        {
            p_target->unregister_comm(m_id);
        }
    }

    void Comm::open(json metadata, json data, buffer_sequence buffers)
    {
        send_comm_message("comm_open", std::move(metadata), std::move(data), std::move(buffers), p_target->name());
    }

    void Comm::close(json metadata, json data, buffer_sequence buffers)
    {
        send_comm_message("comm_close", std::move(metadata), std::move(data), std::move(buffers));
    }

    void Comm::send(json metadata, json data, buffer_sequence buffers) const
    {
        send_comm_message("comm_msg", std::move(metadata), std::move(data), std::move(buffers));
    }

    Guid Comm::id() const noexcept
    {
        return m_id;
    }

    /********************************
     * CommManager implementation *
     ********************************/

    CommManager::CommManager(KernelCore* kernel)
    {
        p_kernel = kernel;
    }

    json CommManager::get_metadata() const
    {
        json metadata;
        metadata["started"] = iso8601_now();
        return metadata;
    }

    void CommManager::register_comm_target(const std::string& target_name,
        const target_function_type& callback)
    {
        m_targets.insert_or_assign(target_name, CommTarget(target_name, callback, this));
    }

    void CommManager::unregister_comm_target(const std::string& target_name)
    {
        m_targets.erase(target_name);
    }

    void CommManager::register_comm(Guid id, Comm* c)
    {
        m_comms[id] = c;
    }

    void CommManager::unregister_comm(Guid id)
    {
        m_comms.erase(id);
    }

    void CommManager::comm_open(Message request)
    {
        const json& content = request.content();
        std::string target_name = content["target_name"];
        auto position = m_targets.find(target_name);

        if (position == m_targets.end())
        {
            if (p_kernel != nullptr)
            {
                p_kernel->publish_message(
                    "comm_close", request.header(), json::object(), content, buffer_sequence(), channel::SHELL
                );
            }
        }
        else
        {
            CommTarget& trg = position->second;
            Guid id = content["comm_id"];
            Comm new_comm(&trg, id);
            trg(std::move(new_comm), std::move(request));
        }
    }

    void CommManager::comm_close(Message request)
    {
        const json& content = request.content();
        Guid id = content["comm_id"];
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

    void CommManager::comm_msg(Message request)
    {
        const json& content = request.content();
        Guid id = content["comm_id"];
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

    const CommTarget* CommManager::target(const std::string& target_name) const
    {
        auto iter = m_targets.find(target_name);
        return iter == m_targets.end() ? nullptr : &(iter->second);
    }

    const std::map<Guid, Comm*>& CommManager::comms() const noexcept
    {
        return m_comms;
    }
}
