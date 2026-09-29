#ifndef THEMISTO_HTTP_API_HPP
#define THEMISTO_HTTP_API_HPP

#include <functional>
#include <string>
#include <thread>

#include <httplib.h>

#include "activity.hpp"
#include "session_registry.hpp"

namespace themisto
{
    // REST surface for session lifecycle only (create/list/get/delete/
    // restart) -- all execute/interrupt/message traffic goes over WsRelay
    // instead, so there's exactly one place doing request/reply
    // correlation rather than splitting state across REST and WS.
    class HttpApi
    {
    public:
        // `token`: what every request must present (see access.hpp); empty
        // turns the check off.
        HttpApi(SessionRegistry& registry, std::string token, Activity& activity);

        // Called (on a server thread) when a client asks the supervisor to
        // shut down (POST /shutdown); main.cpp does the actual stopping.
        void onShutdownRequested(std::function<void()> callback) { m_onShutdown = std::move(callback); }
        ~HttpApi();

        HttpApi(const HttpApi&) = delete;
        HttpApi& operator=(const HttpApi&) = delete;

        // Binds to an OS-assigned port on 127.0.0.1 and serves in a
        // background thread. Returns the bound port.
        int start();
        void stop();

    private:
        SessionRegistry& m_registry;
        std::string m_token;
        Activity& m_activity;
        std::function<void()> m_onShutdown;
        httplib::Server m_server;
        std::thread m_serverThread;
    };
}

#endif
