#ifndef DATASUITE_COMM_HPP
#define DATASUITE_COMM_HPP

#include <functional>
#include <list>
#include <map>
#include <string>
#include <utility>

#include "guid.hpp"
#include "json.hpp"
#include "message.hpp"

namespace datasuite
{
    /*****************************
     * CommTarget declaration *
     *****************************/

    class Comm;
    class CommManager;

    /**
     * @class CommTarget
     * @brief Comm target.
     */
    class DATASUITE_API CommTarget
    {
    public:

        using function_type = std::function<void(Comm&&, Message)>;

        CommTarget(const std::string& name, const function_type& callback, CommManager* manager);

        const std::string& name() const noexcept;

        void operator()(Comm&& c, Message request) const;

        void publish_message(const std::string&, json, json, buffer_sequence) const;

        void register_comm(Guid, Comm*) const;
        void unregister_comm(Guid) const;

    private:

        std::string m_name;
        function_type m_callback;
        CommManager* p_manager;
    };

    /*********************
     * Comm declaration *
     *********************/

     /**
      * @class Comm
      * @brief Comm object.
      *
      */
    class DATASUITE_API Comm
    {
    public:

        using handler_type = std::function<void(Message)>;

        explicit Comm(const CommTarget* target, Guid id = datasuite::new_guid());
        ~Comm();
        Comm(Comm&&);
        Comm(const Comm&);

        Comm& operator=(Comm&&);
        Comm& operator=(const Comm&);

        void open(json metadata, json data, buffer_sequence buffers);
        void close(json metadata, json data, buffer_sequence buffers);
        void send(json metadata, json data, buffer_sequence buffers) const;

        const CommTarget& target() const noexcept;

        void handle_message(Message request);
        void handle_close(Message request);

        Guid id() const noexcept;

        template <class T>
        void on_message(T&& handler);
        template <class T>
        void on_close(T&& handler);

    private:

        friend class CommManager;

        void send_comm_message(const std::string& msg_type,
            json metadata,
            json data,
            buffer_sequence) const;

        void send_comm_message(const std::string& msg_type,
            json metadata,
            json data,
            buffer_sequence,
            const std::string& target_name) const;

        handler_type m_closeHandler;
        handler_type m_messageHandler;
        const CommTarget* p_target;
        Guid m_id;
        bool m_movedFrom;
    };

    /*****************************
     * CommManager declaration *
     *****************************/

    class KernelCore;

    class DATASUITE_API CommManager
    {
    public:

        CommManager(KernelCore* kernel = nullptr);

        using target_function_type = CommTarget::function_type;

        void register_comm_target(const std::string& target_name,
            const target_function_type& callback);
        void unregister_comm_target(const std::string& target_name);

        void comm_open(Message request);
        void comm_close(Message request);
        void comm_msg(Message request);

        const std::map<Guid, Comm*>& comms() const noexcept;

        const CommTarget* target(const std::string& target_name) const;

    private:

        friend class CommTarget;

        void register_comm(Guid, Comm*);
        void unregister_comm(Guid);

        json get_metadata() const;

        std::map<Guid, Comm*> m_comms;
        std::map<std::string, CommTarget> m_targets;
        KernelCore* p_kernel;
    };

    /************************
     * Comm implementation *
     ************************/

    template <class T>
    inline void Comm::on_message(T&& handler)
    {
        m_messageHandler = std::forward<T>(handler);
    }

    template <class T>
    inline void Comm::on_close(T&& handler)
    {
        m_closeHandler = std::forward<T>(handler);
    }
}

#endif
