#ifndef DATASUITE_SUPERVISOR_WS_RELAY_HPP
#define DATASUITE_SUPERVISOR_WS_RELAY_HPP

#include <memory>

#include <ixwebsocket/IXWebSocketServer.h>

#include "session_registry.hpp"

namespace datasuite::supervisor
{
    // One WebSocket connection per session, at /sessions/<id>/messages.
    // Outbound: iopub/shell traffic relayed from Session::onMessage
    // (populated by SessionRegistry's poll thread) plus a kernelExit event
    // on heartbeat-detected crash. Inbound: {type:"execute"|"interrupt", ...}
    // frames forwarded into SessionRegistry::sendExecute/sendInterrupt.
    class WsRelay
    {
    public:
        explicit WsRelay(SessionRegistry& registry);
        ~WsRelay();

        WsRelay(const WsRelay&) = delete;
        WsRelay& operator=(const WsRelay&) = delete;

        // Binds to an OS-assigned port on 127.0.0.1 and starts serving.
        // Returns the bound port.
        int start();
        void stop();

    private:
        SessionRegistry& m_registry;
        std::unique_ptr<ix::WebSocketServer> m_server;
    };
}

#endif
