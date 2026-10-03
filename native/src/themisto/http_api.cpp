#include "http_api.hpp"

#include "access.hpp"

#include <stdexcept>

namespace themisto
{
    namespace
    {
        SessionOptions parseSessionOptions(const json& body)
        {
            SessionOptions options;
            options.kernelType = body.value("kernelType", "r");
            options.rHome = body.value("rHome", "");
            options.rPath = body.value("rPath", "");
            options.rLibs = body.value("rLibs", "");
            options.pandocPath = body.value("pandocPath", "");
            options.pythonHome = body.value("pythonHome", "");
            options.pythonPath = body.value("pythonPath", "");
            options.venvPath = body.value("venvPath", "");
            options.stataHome = body.value("stataHome", "");
            options.stataEdition = body.value("stataEdition", "");
            options.arkPath = body.value("arkPath", "");
            if (body.contains("kernelArgv") && body["kernelArgv"].is_array())
            {
                for (const auto& arg : body["kernelArgv"])
                {
                    if (arg.is_string())
                    {
                        options.kernelArgv.push_back(arg.get<std::string>());
                    }
                }
            }
            options.kernelInterruptMode = body.value("kernelInterruptMode", "");
            if (body.contains("kernelEnv") && body["kernelEnv"].is_object())
            {
                for (const auto& [name, value] : body["kernelEnv"].items())
                {
                    if (value.is_string())
                    {
                        options.kernelEnv[name] = value.get<std::string>();
                    }
                }
            }
            options.workingDirectory = body.value("workingDirectory", "");
            return options;
        }

        void sendJson(httplib::Response& res, int status, const json& body)
        {
            res.status = status;
            res.set_content(body.dump(), "application/json");
        }
    }

    HttpApi::HttpApi(SessionRegistry& registry, std::string token, Activity& activity)
        : m_registry(registry), m_token(std::move(token)), m_activity(activity)
    {
        // Every request needs the supervisor's token, and none may come from
        // a web browser -- see access.hpp.
        m_server.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            switch (access::check(req.get_header_value("Origin"), req.get_header_value("Authorization"), req.target, m_token))
            {
            case access::Verdict::Allowed:
                m_activity.touch();
                return httplib::Server::HandlerResponse::Unhandled;
            case access::Verdict::FromBrowser:
                sendJson(res, 403, { { "error", "requests from web pages are not accepted" } });
                return httplib::Server::HandlerResponse::Handled;
            case access::Verdict::BadToken:
            default:
                sendJson(res, 401, { { "error", "missing or wrong access token" } });
                return httplib::Server::HandlerResponse::Handled;
            }
        });

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

            SessionOptions options = parseSessionOptions(body);
            std::string error;
            std::string id = m_registry.createSession(options, error);
            if (id.empty())
            {
                sendJson(res, 500, { { "error", error } });
                return;
            }
            auto session = m_registry.getSession(id);
            sendJson(res, 200, session ? sessionToJson(*session)
                                       : json{ { "sessionId", id }, { "status", "ready" }, { "kernelType", options.kernelType } });
        });

        // Stops every session and then the supervisor itself (answered
        // first; the stopping happens on main.cpp's thread).
        m_server.Post("/shutdown", [this](const httplib::Request&, httplib::Response& res) {
            sendJson(res, 200, { { "status", "shutting down" } });
            if (m_onShutdown) m_onShutdown();
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
            sendJson(res, 200, sessionToJson(*session));
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
            // An optional JSON body, same shape as POST /sessions, lets a
            // restart switch R installations (e.g. a different rHome) for
            // an existing session in place -- an empty body preserves the
            // old behavior of just reusing whatever options this session
            // was originally created with.
            std::optional<SessionOptions> newOptions;
            if (!req.body.empty())
            {
                json body;
                try
                {
                    body = json::parse(req.body);
                }
                catch (const std::exception&)
                {
                    sendJson(res, 400, { { "error", "invalid JSON body" } });
                    return;
                }
                newOptions = parseSessionOptions(body);
            }

            std::string error;
            std::string id = m_registry.restartSession(req.matches[1], error, newOptions);
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
