#include "http_api.hpp"

#include <stdexcept>

namespace datasuite::supervisor
{
    namespace
    {
        SessionOptions parseSessionOptions(const json& body)
        {
            SessionOptions options;
            options.rHome = body.value("rHome", "");
            options.rPath = body.value("rPath", "");
            options.rLibs = body.value("rLibs", "");
            options.pandocPath = body.value("pandocPath", "");
            options.heraSrcPath = body.value("heraSrcPath", "");
            return options;
        }

        void sendJson(httplib::Response& res, int status, const json& body)
        {
            res.status = status;
            res.set_content(body.dump(), "application/json");
        }
    }

    HttpApi::HttpApi(SessionRegistry& registry) : m_registry(registry)
    {
        m_server.Post("/sessions", [this](const httplib::Request& req, httplib::Response& res) {
            json body = json::object();
            try
            {
                if (!req.body.empty())
                {
                    body = json::parse(req.body);
                }
            }
            catch (const std::exception&)
            {
                sendJson(res, 400, { { "error", "invalid JSON body" } });
                return;
            }

            std::string error;
            std::string id = m_registry.createSession(parseSessionOptions(body), error);
            if (id.empty())
            {
                sendJson(res, 500, { { "error", error } });
                return;
            }
            sendJson(res, 200, { { "sessionId", id }, { "status", "ready" } });
        });

        m_server.Get("/sessions", [this](const httplib::Request&, httplib::Response& res) {
            sendJson(res, 200, { { "sessions", m_registry.listSessions() } });
        });

        m_server.Get(R"(/sessions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            auto session = m_registry.getSession(req.matches[1]);
            if (!session)
            {
                sendJson(res, 404, { { "error", "session not found" } });
                return;
            }
            sendJson(res, 200, { { "sessionId", session->id }, { "status", toString(session->status.load()) } });
        });

        m_server.Delete(R"(/sessions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
            std::string id = req.matches[1];
            if (!m_registry.stopSession(id))
            {
                sendJson(res, 404, { { "error", "session not found" } });
                return;
            }
            sendJson(res, 200, { { "sessionId", id }, { "status", "stopped" } });
        });

        m_server.Post(R"(/sessions/([^/]+)/restart)", [this](const httplib::Request& req, httplib::Response& res) {
            std::string error;
            std::string id = m_registry.restartSession(req.matches[1], error);
            if (id.empty())
            {
                sendJson(res, 500, { { "error", error } });
                return;
            }
            sendJson(res, 200, { { "sessionId", id }, { "status", "ready" } });
        });
    }

    HttpApi::~HttpApi()
    {
        stop();
    }

    int HttpApi::start()
    {
        int port = m_server.bind_to_any_port("127.0.0.1");
        m_serverThread = std::thread([this]() { m_server.listen_after_bind(); });
        return port;
    }

    void HttpApi::stop()
    {
        if (m_server.is_running())
        {
            m_server.stop();
        }
        if (m_serverThread.joinable())
        {
            m_serverThread.join();
        }
    }
}
