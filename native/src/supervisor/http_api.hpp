#ifndef DATASUITE_SUPERVISOR_HTTP_API_HPP
#define DATASUITE_SUPERVISOR_HTTP_API_HPP

#include <thread>

#include <httplib.h>

#include "session_registry.hpp"

namespace datasuite::supervisor
{
    // REST surface for session lifecycle only (create/list/get/delete/
    // restart) -- all execute/interrupt/message traffic goes over WsRelay
    // instead, so there's exactly one place doing request/reply
    // correlation rather than splitting state across REST and WS.
    class HttpApi
    {
    public:
        explicit HttpApi(SessionRegistry& registry);
        ~HttpApi();

        HttpApi(const HttpApi&) = delete;
        HttpApi& operator=(const HttpApi&) = delete;

        // Binds to an OS-assigned port on 127.0.0.1 and serves in a
        // background thread. Returns the bound port.
        int start();
        void stop();

    private:
        SessionRegistry& m_registry;
        httplib::Server m_server;
        std::thread m_serverThread;
    };
}

#endif
