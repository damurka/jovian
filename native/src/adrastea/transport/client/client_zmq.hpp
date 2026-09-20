#ifndef ADRASTEA_CLIENT_ZMQ_HPP
#define ADRASTEA_CLIENT_ZMQ_HPP

#include <optional>

#include "adrastea/adrastea.hpp"
#include "adrastea/json.hpp"
#include "adrastea/context.hpp"
#include "adrastea/kernel_configuration.hpp"
#include "adrastea/message.hpp"

namespace adrastea
{
    class ClientZmqImpl;

    class ADRASTEA_API ClientZmq
    {
    public:

        using listener = std::function<void(Message)>;
        using iopub_listener = std::function<void(PubMessage)>;
        using kernel_status_listener = std::function<void(bool)>;

        explicit ClientZmq(std::unique_ptr<ClientZmqImpl> impl);
        ~ClientZmq();

        void connect();
        void start();
        void stopChannels();

        void sendOnShell(Message msg);
        void sendOnControl(Message msg);

        // APIs for receiving on a specified channel
        std::optional<Message> receiveOnShell(bool blocking = true);
        std::optional<Message> receiveOnControl(bool blocking = true);

        std::size_t iopubQueueSize() const;
        std::optional<PubMessage> popIopubMessage();

        // APIs for receiving on all channels
        void registerShellListener(const listener& l);
        void registerControlListener(const listener& l);
        void registerIopubListener(const iopub_listener& l);
        void registerKernelStatusListener(const kernel_status_listener& l);

        void waitForMessage();

    private:

        std::unique_ptr<ClientZmqImpl> p_clientImpl;
    };

    ADRASTEA_API
    std::unique_ptr<ClientZmq> makeClientZmq(Context& context,
            const KernelConfiguration& config,
            json::error_handler_t eh = json::error_handler_t::strict);
}

#endif
