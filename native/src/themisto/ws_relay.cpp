#include "ws_relay.hpp"

#include <regex>
#include <stdexcept>

#include "adrastea/middleware.hpp"

namespace themisto
{
    namespace
    {
        std::string extractSessionId(const std::string& uri)
        {
            static const std::regex pattern(R"(^/sessions/([^/]+)/messages/?$)");
            std::smatch match;
            if (std::regex_match(uri, match, pattern))
            {
                return match[1];
            }
            return std::string();
        }

        // Per-connection state: ixwebsocket registers one onMessageCallback
        // per connection (fired for every Open/Message/Close/Error event on
        // it), so the session this connection is bound to needs to persist
        // across those calls rather than being re-resolved each time.
        struct ConnectionState
        {
            std::shared_ptr<Session> session;
            std::string sessionId;
        };
    }

    WsRelay::WsRelay(SessionRegistry& registry) : m_registry(registry) {}

    WsRelay::~WsRelay()
    {
        stop();
    }

    int WsRelay::start()
    {
        // Not port 0: unlike cpp-httplib's bind_to_any_port() (used in
        // http_api.cpp), ix::SocketServer::getPort() just echoes back
        // whatever port its constructor was given -- it doesn't query the
        // OS for the real ephemeral port after an OS-assigned (0) bind. Pick
        // one explicitly instead, the same way ZMQ ports are already picked
        // elsewhere in this codebase (see findFreePort() usage in
        // elara.cpp/engine.cpp).
        int port = std::stoi(adrastea::findFreePort());
        m_server = std::make_unique<ix::WebSocketServer>(port, "127.0.0.1");

        m_server->setOnConnectionCallback(
            [this](std::weak_ptr<ix::WebSocket> weakWebSocket, std::shared_ptr<ix::ConnectionState>) {
                auto webSocket = weakWebSocket.lock();
                if (!webSocket)
                {
                    return;
                }

                auto state = std::make_shared<ConnectionState>();

                webSocket->setOnMessageCallback(
                    [this, weakWebSocket, state](const ix::WebSocketMessagePtr& msg) {
                        if (msg->type == ix::WebSocketMessageType::Open)
                        {
                            state->sessionId = extractSessionId(msg->openInfo.uri);
                            state->session = m_registry.getSession(state->sessionId);

                            if (!state->session)
                            {
                                if (auto ws = weakWebSocket.lock())
                                {
                                    ws->close(1008, "unknown session");
                                }
                                return;
                            }

                            {
                                std::lock_guard<std::mutex> lock(state->session->callbackMutex);
                                state->session->onMessage = [weakWebSocket](const std::string& text) {
                                    if (auto ws = weakWebSocket.lock())
                                    {
                                        ws->send(text);
                                    }
                                };
                                state->session->onKernelExit = [weakWebSocket](const std::string& reason) {
                                    if (auto ws = weakWebSocket.lock())
                                    {
                                        ws->send(json{ { "type", "kernelExit" }, { "reason", reason } }.dump());
                                    }
                                };
                            }

                            if (auto ws = weakWebSocket.lock())
                            {
                                ws->send(json{ { "type", "ready" } }.dump());
                            }
                        }
                        else if (msg->type == ix::WebSocketMessageType::Message)
                        {
                            if (!state->session)
                            {
                                return;
                            }
                            try
                            {
                                json frame = json::parse(msg->str);
                                std::string type = frame.value("type", "");
                                std::string id = frame.value("id", "");
                                if (type == "execute")
                                {
                                    m_registry.sendExecute(
                                        state->sessionId, id, frame.value("code", ""), frame.value("options", json::object()));
                                }
                                else if (type == "interrupt")
                                {
                                    m_registry.sendInterrupt(state->sessionId, id);
                                }
                                else if (type == "inputReply")
                                {
                                    m_registry.sendInputReply(state->sessionId, frame.value("value", ""));
                                }
                                else if (type == "history")
                                {
                                    m_registry.sendHistory(state->sessionId, id, frame.value("options", json::object()));
                                }
                            }
                            catch (const std::exception&)
                            {
                                // Malformed frame -- ignore rather than tearing
                                // down the connection over one bad message.
                            }
                        }
                        else if (msg->type == ix::WebSocketMessageType::Close)
                        {
                            if (state->session)
                            {
                                std::lock_guard<std::mutex> lock(state->session->callbackMutex);
                                state->session->onMessage = nullptr;
                                state->session->onKernelExit = nullptr;
                            }
                        }
                    });
            });

        auto result = m_server->listen();
        if (!result.first)
        {
            throw std::runtime_error("Failed to bind WebSocket server: " + result.second);
        }
        m_server->start();
        return port;
    }

    void WsRelay::stop()
    {
        if (m_server)
        {
            m_server->stop();
        }
    }
}
