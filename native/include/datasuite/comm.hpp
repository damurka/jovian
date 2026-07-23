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

        void publishMessage(const std::string&, json, json, buffer_sequence) const;

        void registerComm(Guid, Comm*) const;
        void unregisterComm(Guid) const;

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

        explicit Comm(const CommTarget* target, Guid id = datasuite::newGuid());
        ~Comm();
        Comm(Comm&&);
        Comm(const Comm&);

        Comm& operator=(Comm&&);
        Comm& operator=(const Comm&);

        void open(json metadata, json data, buffer_sequence buffers);
        void close(json metadata, json data, buffer_sequence buffers);
        void send(json metadata, json data, buffer_sequence buffers) const;

        const CommTarget& target() const noexcept;

        void handleMessage(Message request);
        void handleClose(Message request);

        Guid id() const noexcept;

        template <class T>
        void onMessage(T&& handler);
        template <class T>
        void onClose(T&& handler);

    private:

        friend class CommManager;

        void sendCommMessage(const std::string& msg_type,
            json metadata,
            json data,
            buffer_sequence) const;

        void sendCommMessage(const std::string& msg_type,
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

        void registerCommTarget(const std::string& target_name,
            const target_function_type& callback);
        void unregisterCommTarget(const std::string& target_name);

        void commOpen(Message request);
        void commClose(Message request);
        void commMsg(Message request);

        const std::map<Guid, Comm*>& comms() const noexcept;

        const CommTarget* target(const std::string& target_name) const;

    private:

        friend class CommTarget;

        void registerComm(Guid, Comm*);
        void unregisterComm(Guid);

        json getMetadata() const;

        std::map<Guid, Comm*> m_comms;
        std::map<std::string, CommTarget> m_targets;
        KernelCore* p_kernel;
    };

    /************************
     * Comm implementation *
     ************************/

    template <class T>
    inline void Comm::onMessage(T&& handler)
    {
        m_messageHandler = std::forward<T>(handler);
    }

    template <class T>
    inline void Comm::onClose(T&& handler)
    {
        m_closeHandler = std::forward<T>(handler);
    }
}

#endif
