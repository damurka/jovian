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

namespace nl = nlohmann;

namespace datasuite
{
    /*****************************
     * comm_target declaration *
     *****************************/

    class comm;
    class comm_manager;

    /**
     * @class comm_target
     * @brief Comm target.
     */
    class DATASUITE_API comm_target
    {
    public:

        using function_type = std::function<void(comm&&, message)>;

        comm_target(const std::string& name, const function_type& callback, comm_manager* manager);

        const std::string& name() const noexcept;

        void operator()(comm&& c, message request) const;

        void publish_message(const std::string&, nl::json, nl::json, buffer_sequence) const;

        void register_comm(guid, comm*) const;
        void unregister_comm(guid) const;

    private:

        std::string m_name;
        function_type m_callback;
        comm_manager* p_manager;
    };

    /*********************
     * comm declaration *
     *********************/

     /**
      * @class comm
      * @brief Comm object.
      *
      */
    class DATASUITE_API comm
    {
    public:

        using handler_type = std::function<void(message)>;

        explicit comm(const comm_target* target, guid id = datasuite::new_guid());
        ~comm();
        comm(comm&&);
        comm(const comm&);

        comm& operator=(comm&&);
        comm& operator=(const comm&);

        void open(nl::json metadata, nl::json data, buffer_sequence buffers);
        void close(nl::json metadata, nl::json data, buffer_sequence buffers);
        void send(nl::json metadata, nl::json data, buffer_sequence buffers) const;

        const comm_target& target() const noexcept;

        void handle_message(message request);
        void handle_close(message request);

        guid id() const noexcept;

        template <class T>
        void on_message(T&& handler);
        template <class T>
        void on_close(T&& handler);

    private:

        friend class comm_manager;

        void send_comm_message(const std::string& msg_type,
            nl::json metadata,
            nl::json data,
            buffer_sequence) const;

        void send_comm_message(const std::string& msg_type,
            nl::json metadata,
            nl::json data,
            buffer_sequence,
            const std::string& target_name) const;

        handler_type m_close_handler;
        handler_type m_message_handler;
        const comm_target* p_target;
        guid m_id;
        bool m_moved_from;
    };

    /*****************************
     * comm_manager declaration *
     *****************************/

    class kernel_core;

    class DATASUITE_API comm_manager
    {
    public:

        comm_manager(kernel_core* kernel = nullptr);

        using target_function_type = comm_target::function_type;

        void register_comm_target(const std::string& target_name,
            const target_function_type& callback);
        void unregister_comm_target(const std::string& target_name);

        void comm_open(message request);
        void comm_close(message request);
        void comm_msg(message request);

        const std::map<guid, comm*>& comms() const noexcept;

        const comm_target* target(const std::string& target_name) const;

    private:

        friend class comm_target;

        void register_comm(guid, comm*);
        void unregister_comm(guid);

        nl::json get_metadata() const;

        std::map<guid, comm*> m_comms;
        std::map<std::string, comm_target> m_targets;
        kernel_core* p_kernel;
    };

    /************************
     * comm implementation *
     ************************/

    template <class T>
    inline void comm::on_message(T&& handler)
    {
        m_message_handler = std::forward<T>(handler);
    }

    template <class T>
    inline void comm::on_close(T&& handler)
    {
        m_close_handler = std::forward<T>(handler);
    }
}

#endif