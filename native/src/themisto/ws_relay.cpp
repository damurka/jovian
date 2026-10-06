#include "ws_relay.hpp"

#include "access.hpp"
#include "outbox.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
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
            const std::string path = access::pathOf(uri);
            if (std::regex_match(path, match, pattern))
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
            // Counted in Activity::openConnections() (only accepted ones).
            bool counted = false;
            // Everything sent to this client goes through here (outbox.hpp).
            std::shared_ptr<Outbox> outbox;
        };
    }

    WsRelay::WsRelay(SessionRegistry& registry, std::string token, Activity& activity)
        : m_registry(registry), m_token(std::move(token)), m_activity(activity) {}

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
        // No compression: the client is on this machine, so there is no network to spare, and deflating a large
        // message cost far more than sending it (a 4 MB page of numbers: 650 ms of the 740 ms the request took).
        m_server->disablePerMessageDeflate();

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
                            // The same rule as the HTTP API (access.hpp).
                            // ixwebsocket has no hook before the upgrade,
                            // so a refused connection is closed at once and
                            // never bound to a session.
                            auto header = [&](const char* name) {
                                for (const auto& [key, value] : msg->openInfo.headers)
                                {
                                    if (key.size() == std::strlen(name) &&
                                        std::equal(key.begin(), key.end(), name, [](char a, char b) {
                                            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                                        }))
                                    {
                                        return value;
                                    }
                                }
                                return std::string();
                            };
                            auto verdict = access::check(header("Origin"), header("Authorization"), msg->openInfo.uri, m_token);
                            if (verdict != access::Verdict::Allowed)
                            {
                                if (auto ws = weakWebSocket.lock())
                                {
                                    ws->close(verdict == access::Verdict::FromBrowser ? 4403 : 4401,
                                        verdict == access::Verdict::FromBrowser ? "requests from web pages are not accepted"
                                                                                : "missing or wrong access token");
                                }
                                return;
                            }

                            state->counted = true;
                            m_activity.connectionOpened();
                            state->outbox = Outbox::start(
                                [weakWebSocket](const std::string& frame) {
                                    if (auto ws = weakWebSocket.lock()) ws->send(frame);
                                },
                                [weakWebSocket]() -> std::size_t {
                                    auto ws = weakWebSocket.lock();
                                    return ws ? ws->bufferedAmount() : 0;
                                });
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
                                // Called on the session's poll thread with
                                // callbackMutex held: only a push, never a send.
                                std::weak_ptr<Outbox> outbox = state->outbox;
                                state->session->onMessage = [outbox](const std::string& text) {
                                    if (auto o = outbox.lock()) o->push(text);
                                };
                                state->session->onKernelExit = [outbox](const std::string& reason) {
                                    if (auto o = outbox.lock()) o->push(json{ { "type", "kernelExit" }, { "reason", reason } }.dump());
                                };
                                state->session->callbackOwner = state->outbox.get();
                            }

                            state->outbox->push(json{ { "type", "ready" } }.dump());
                        }
                        else if (msg->type == ix::WebSocketMessageType::Message)
                        {
                            m_activity.touch();
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
                                else if (type == "inputReply")
                                {
                                    m_registry.sendInputReply(state->sessionId, frame.value("value", ""));
                                }
                                else if (type == "request")
                                {
                                    // Any Jupyter request that has a plain
                                    // request/reply (or comm) shape:
                                    // complete/inspect/is_complete/
                                    // kernel_info/history/comm_* on shell,
                                    // interrupt on control. The reply comes
                                    // back as an ordinary "message" frame
                                    // whose parent_msg_id is `id`; a request
                                    // that can't even be sent is reported
                                    // straight away instead of leaving the
                                    // caller waiting for a reply that will
                                    // never exist.
                                    std::string error;
                                    if (!m_registry.sendRequest(state->sessionId, frame.value("channel", "shell"),
                                                                frame.value("msgType", ""), id,
                                                                frame.value("content", json::object()), error))
                                    {
                                        state->outbox->push(json{ { "type", "requestError" }, { "id", id }, { "error", error } }.dump());
                                    }
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
                            if (state->counted)
                            {
                                state->counted = false;
                                m_activity.connectionClosed();
                            }
                            if (state->session)
                            {
                                std::lock_guard<std::mutex> lock(state->session->callbackMutex);
                                // Only if still ours: a newer connection may
                                // have taken the session over already.
                                if (state->outbox && state->session->callbackOwner == state->outbox.get())
                                {
                                    state->session->onMessage = nullptr;
                                    state->session->onKernelExit = nullptr;
                                    state->session->callbackOwner = nullptr;
                                }
                            }
                            if (state->outbox)
                            {
                                state->outbox->abandon();
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
