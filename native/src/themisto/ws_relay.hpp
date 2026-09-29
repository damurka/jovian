#ifndef THEMISTO_WS_RELAY_HPP
#define THEMISTO_WS_RELAY_HPP

#include <memory>
#include <string>

#include <ixwebsocket/IXWebSocketServer.h>

#include "activity.hpp"
#include "session_registry.hpp"

namespace themisto
{
    // One WebSocket connection per session, at /sessions/<id>/messages.
    // Outbound: iopub/shell traffic relayed from Session::onMessage
    // (populated by SessionRegistry's poll thread) plus a kernelExit event
    // on heartbeat-detected crash. Inbound: {type:"execute"|"inputReply"|"request", ...}
    // frames forwarded into SessionRegistry::sendExecute/sendInputReply/
    // sendRequest. A "request" frame ({id, channel, msgType, content}) that
    // can't be sent is answered with {type:"requestError", id, error}.
    class WsRelay
    {
    public:
        // `token`: what every connection must present, as ?token= or an
        // Authorization header (see access.hpp); empty turns the check off.
        WsRelay(SessionRegistry& registry, std::string token, Activity& activity);
        ~WsRelay();

        WsRelay(const WsRelay&) = delete;
        WsRelay& operator=(const WsRelay&) = delete;

        // Binds to an OS-assigned port on 127.0.0.1 and starts serving.
        // Returns the bound port.
        int start();
        void stop();

    private:
        SessionRegistry& m_registry;
        std::string m_token;
        Activity& m_activity;
        std::unique_ptr<ix::WebSocketServer> m_server;
    };
}

#endif
