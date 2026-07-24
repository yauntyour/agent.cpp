#ifdef AGENT_ENABLE_ROUTER
#include "components/router/router.hpp"
#include "components/session/session.hpp"
#include "components/agent/agent.hpp"
#include "components/system/system.hpp"

#include <servic.hpp>
#include <router/router.hpp>

#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <sstream>

namespace agent {

using json = nlohmann::json;
namespace asio = boost::asio;

static std::string extract_json_body(const std::string& raw_request) {
    size_t pos = raw_request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return raw_request.substr(pos + 4);
}

static std::string make_http_response(const std::string& body, int code = 200) {
    std::string status = (code == 200) ? "200 OK" :
                         (code == 401) ? "401 Unauthorized" :
                         (code == 400) ? "400 Bad Request" :
                         (code == 404) ? "404 Not Found" : "500 Internal Server Error";
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "\r\n" + body;
}

struct RouterImpl {
    std::unique_ptr<asio::io_context> io_ctx;
    std::unique_ptr<servic::Server> server;
    rt::router ros;
    std::thread worker;
};

Router::Router() : m_impl(std::make_unique<RouterImpl>()) {}
Router::~Router() = default;

void Router::on_initialize() {
    register_default_routes();
}

void Router::on_shutdown() {
    stop();
}

void Router::configure(const RouterConfig& config) {
    m_config = config;
}

RouterConfig Router::get_config() const {
    return m_config;
}

void Router::add_route(const Route& route) {
    m_routes.push_back(route);
}

void Router::add_stream_route(const StreamRoute& route) {
    m_stream_routes.push_back(route);
}

void Router::clear_routes() {
    m_routes.clear();
    m_stream_routes.clear();
}

void Router::start() {
    if (m_running.load()) return;

    m_impl->io_ctx = std::make_unique<asio::io_context>();
    m_impl->server = std::make_unique<servic::Server>(*m_impl->io_ctx,
        static_cast<short>(m_config.port), 300000);

    for (const auto& route : m_routes) {
        auto handler = route.handler;
        bool auth = route.requires_auth;

        m_impl->ros.on(route.path,
            [handler, auth, this](std::string& raw_request, std::string& raw_response,
                                  const std::map<std::string, std::string>& params) -> int {
                std::string body = extract_json_body(raw_request);

                json req_json;
                if (!body.empty()) {
                    try {
                        req_json = json::parse(body);
                    } catch (const std::exception&) {
                        auto err = error_response("Invalid JSON in request body");
                        raw_response = make_http_response(err.dump());
                        return rt::FLAG_DONE;
                    }
                }

                if (auth && !m_config.password_hash.empty()) {
                    raw_response = make_http_response(
                        error_response("Authentication required", 401).dump(), 401);
                    return rt::FLAG_DONE;
                }

                try {
                    json result = handler(req_json);
                    raw_response = make_http_response(result.dump());
                } catch (const std::exception& e) {
                    raw_response = make_http_response(
                        error_response(e.what()).dump(), 500);
                }
                return rt::FLAG_DONE;
            });
    }

    for (const auto& sroute : m_stream_routes) {
        auto handler = sroute.handler;
        bool auth = sroute.requires_auth;

        m_impl->ros.on_stream(sroute.path,
            [handler, auth, this](std::string& raw_request, rt::WriteCallback write,
                                  const std::map<std::string, std::string>& params) {
                std::string body = extract_json_body(raw_request);

                json req_json;
                if (!body.empty()) {
                    try {
                        req_json = json::parse(body);
                    } catch (const std::exception&) {
                        write("HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nInvalid JSON\n");
                        return;
                    }
                }

                if (auth && !m_config.password_hash.empty()) {
                    write("HTTP/1.1 401 Unauthorized\r\nContent-Type: text/plain\r\n\r\nAuth required\n");
                    return;
                }

                write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");

                try {
                    handler(req_json, [&write](std::string_view chunk) {
                        std::string data(chunk);
                        std::string sse = "data: " + data + "\n\n";
                        write(sse);
                    });
                    write("data: [DONE]\n\n");
                } catch (const std::exception& e) {
                    write("data: [ERROR] " + std::string(e.what()) + "\n\n");
                }
            });
    }

    m_impl->worker = std::thread([this]() {
        try {
            m_impl->server->run(m_impl->ros);
        } catch (const std::exception& e) {
            std::cerr << "Router server exception: " << e.what() << std::endl;
        }
    });

    m_running.store(true);
}

void Router::stop() {
    if (!m_running.load()) return;
    m_running.store(false);

    if (m_impl->io_ctx) {
        m_impl->io_ctx->stop();
    }

    if (m_impl->worker.joinable()) {
        m_impl->worker.join();
    }

    m_impl->io_ctx.reset();
    m_impl->server.reset();
}

void Router::restart() {
    stop();
    start();
}

bool Router::is_running() const {
    return m_running.load();
}

bool Router::authenticate(std::string_view password) const {
    if (m_config.password_hash.empty()) return true;
    return crypto::verify_password(password, m_config.password_hash);
}

void Router::set_password(std::string_view password) {
    m_config.password_hash = crypto::hash_password(password);
}

bool Router::verify_auth_token(std::string_view token) const {
    return !m_config.password_hash.empty();
}

json Router::success_response(const json& data) {
    return {{"success", true}, {"data", data}};
}

json Router::error_response(std::string_view message, int code) {
    return {{"success", false}, {"error", message}, {"code", code}};
}

void Router::register_default_routes() {
    add_route({"GET", "/", [this](const json& req) -> json {
        return success_response({{"service", "agent.cpp"}, {"version", AGENT_VERSION}});
    }, false});

    add_route({"GET", "/api/health", [this](const json& req) -> json {
        auto& sys = ModuleRegistry::instance().require<System>();
        return success_response(sys.system_info());
    }, false});

    add_route({"POST", "/api/input", [this](const json& req) -> json {
        if (!req.contains("text")) return error_response("text is required");
        auto& agent = ModuleRegistry::instance().require<Agent>();
        auto result = agent.execute(req["text"].get<std::string>(), nullptr, false);
        return success_response({
            {"output", result.output},
            {"iterations", result.iterations_used},
            {"success", result.success}
        });
    }});

    add_stream_route({"POST", "/api/input/stream", [this](const json& req, auto writer) {
        if (!req.contains("text")) {
            writer("Error: text is required\n");
            return;
        }
        auto& agent = ModuleRegistry::instance().require<Agent>();
        agent.execute(req["text"].get<std::string>(),
            [&](std::string_view type, std::string_view content) {
                if (type == "thinking") writer(content);
                else if (type == "done") writer("\n[DONE]\n");
            }, true);
    }});

    add_route({"GET", "/api/session", [this](const json& req) -> json {
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        auto& sys = ModuleRegistry::instance().require<System>();
        auto list = session.list_sessions(sys.project_dir().string());
        json result = json::array();
        for (auto& s : list) {
            result.push_back({
                {"id", s.id}, {"name", s.name},
                {"message_count", s.message_count}, {"created_at", s.created_at}
            });
        }
        return success_response(result);
    }});

    add_route({"POST", "/api/session/new", [this](const json& req) -> json {
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        auto& sys = ModuleRegistry::instance().require<System>();
        auto name = req.value("name", "");
        auto info = session.new_session(sys.project_dir().string(), name);
        return success_response({{"id", info.id}, {"name", info.name}});
    }});

    add_route({"POST", "/api/session/delete", [this](const json& req) -> json {
        if (!req.contains("id")) return error_response("id is required");
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        session.delete_session(req["id"].get<std::string>());
        return success_response();
    }});

    add_route({"GET", "/api/models", [this](const json& req) -> json {
        auto& provider = ModuleRegistry::instance().require<Provider>();
        auto models = provider.list_models("default");
        json result = json::array();
        for (auto& m : models) {
            result.push_back({
                {"id", m.id}, {"name", m.name},
                {"context_length", m.context_length},
                {"supports_vision", m.supports_vision},
                {"supports_tools", m.supports_tools}
            });
        }
        return success_response(result);
    }});

    add_route({"GET", "/api/tools", [this](const json& req) -> json {
        auto& tools = ModuleRegistry::instance().require<Tools>();
        auto list = tools.list_tools();
        json result = json::array();
        for (auto& t : list) {
            result.push_back({
                {"name", t.name}, {"description", t.description},
                {"categories", t.categories}
            });
        }
        return success_response(result);
    }});

    add_route({"POST", "/api/login", [this](const json& req) -> json {
        if (!req.contains("password")) return error_response("password is required", 401);
        if (authenticate(req["password"].get<std::string>())) {
            return success_response({{"authenticated", true}});
        }
        return error_response("Invalid password", 401);
    }, false});
}

} // namespace agent
#endif