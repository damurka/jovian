#ifndef DATASUITE_CLIENT_ZMQ_HPP
#define DATASUITE_CLIENT_ZMQ_HPP

#include <optional>

#include "datasuite/datasuite.hpp"
#include "datasuite/json.hpp"
#include "datasuite/context.hpp"
#include "datasuite/kernel_configuration.hpp"
#include "datasuite/message.hpp"

namespace datasuite
{
    class client_zmq_impl;

    class DATASUITE_API ClientZmq
    {
    public:

        using listener = std::function<void(message)>;
        using iopub_listener = std::function<void(pub_message)>;
        using kernel_status_listener = std::function<void(bool)>;

        explicit ClientZmq(std::unique_ptr<client_zmq_impl> impl);
        ~ClientZmq();

        void connect();
        void start();
        void stop_channels();

        void send_on_shell(message msg);
        void send_on_control(message msg);

        // APIs for receiving on a specified channel
        std::optional<message> receive_on_shell(bool blocking = true);
        std::optional<message> receive_on_control(bool blocking = true);

        std::size_t iopub_queue_size() const;
        std::optional<pub_message> pop_iopub_message();

        // APIs for receiving on all channels
        void register_shell_listener(const listener& l);
        void register_control_listener(const listener& l);
        void register_iopub_listener(const iopub_listener& l);
        void register_kernel_status_listener(const kernel_status_listener& l);

        void wait_for_message();

    private:

        std::unique_ptr<client_zmq_impl> p_clientImpl;
    };

    DATASUITE_API
    std::unique_ptr<ClientZmq> make_client_zmq(context& context,
            const KernelConfiguration& config,
            json::error_handler_t eh = json::error_handler_t::strict);
}

#endif
