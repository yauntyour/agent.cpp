#ifdef AGENT_ENABLE_ROUTER
#include "components/router/router.hpp"
#include "components/session/session.hpp"
#include "components/agent/agent.hpp"
#include "components/system/system.hpp"
#include "core/logger.hpp"
#include "core/exception.hpp"
#include "utils/crypto.hpp"

#include "external/servic/servic.hpp"

#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace agent {

using json = nlohmann::json;
namespace asio = boost::asio;

static std::string extract_json_body(const std::string& raw_request) {
    size_t pos = raw_request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return raw_request.substr(pos + 4);
}

static std::string make_http_response(const std::string& body, int code = 200,
                                       const std::string& content_type = "application/json",
                                       const std::map<std::string, std::string>& extra_headers = {}) {
    std::string status;
    switch (code) {
    case 200: status = "200 OK"; break;
    case 201: status = "201 Created"; break;
    case 204: status = "204 No Content"; break;
    case 400: status = "400 Bad Request"; break;
    case 401: status = "401 Unauthorized"; break;
    case 403: status = "403 Forbidden"; break;
    case 404: status = "404 Not Found"; break;
    case 405: status = "405 Method Not Allowed"; break;
    default:  status = "500 Internal Server Error"; break;
    }

    std::string resp = "HTTP/1.1 " + status + "\r\n"
                       "Content-Type: " + content_type + "\r\n"
                       "Content-Length: " + std::to_string(body.size()) + "\r\n"
                       "Connection: close\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                       "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                       "Access-Control-Max-Age: 86400\r\n";

    for (auto& [k, v] : extra_headers) {
        resp += k + ": " + v + "\r\n";
    }

    resp += "\r\n" + body;
    return resp;
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

std::string Router::generate_session_token() {
    return crypto::random_bytes_base64(32);
}

std::string Router::extract_header(const std::string& raw_request, std::string_view header_name) {
    std::string search = std::string(header_name) + ": ";
    std::string lower_request = raw_request;
    std::string lower_search = search;
    std::transform(lower_request.begin(), lower_request.end(), lower_request.begin(), ::tolower);
    std::transform(lower_search.begin(), lower_search.end(), lower_search.begin(), ::tolower);

    size_t pos = lower_request.find(lower_search);
    if (pos == std::string::npos) return "";

    size_t val_start = pos + search.size();
    size_t val_end = raw_request.find("\r\n", val_start);
    if (val_end == std::string::npos) val_end = raw_request.size();

    return raw_request.substr(val_start, val_end - val_start);
}

std::map<std::string, std::string> Router::extract_all_headers(const std::string& raw_request) {
    std::map<std::string, std::string> headers;
    size_t header_end = raw_request.find("\r\n\r\n");
    if (header_end == std::string::npos) header_end = raw_request.size();

    std::string header_block = raw_request.substr(0, header_end);
    std::istringstream stream(header_block);
    std::string line;
    bool first = true;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (first) { first = false; continue; }

        auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            while (!val.empty() && val.front() == ' ') val.erase(0, 1);
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            headers[key] = val;
        }
    }
    return headers;
}

void Router::start() {
    if (m_running.load()) return;

    m_impl->io_ctx = std::make_unique<asio::io_context>();
    m_impl->server = std::make_unique<servic::Server>(*m_impl->io_ctx,
        static_cast<short>(m_config.port), 300000);

    for (const auto& route : m_routes) {
        auto handler = route.handler;
        bool auth = route.requires_auth;
        std::string route_path = route.path;

        m_impl->ros.on(route.path,
            [handler, auth, route_path, this](std::string& raw_request, std::string& raw_response,
                                    const std::map<std::string, std::string>& params) -> int {
                auto headers = extract_all_headers(raw_request);

                std::string method;
                {
                    std::istringstream iss(raw_request);
                    iss >> method;
                }

                if (method == "OPTIONS" && m_config.enable_cors) {
                    raw_response = make_http_response("", 204);
                    return rt::FLAG_DONE;
                }

                std::string body = extract_json_body(raw_request);

                json req_json;
                if (!body.empty()) {
                    try {
                        req_json = json::parse(body);
                    } catch (const json::parse_error& e) {
                        LOG_WARN("Router", std::string("Invalid JSON in request body: ") + e.what());
                        raw_response = make_http_response(
                            error_response("Invalid JSON in request body").dump(), 400);
                        return rt::FLAG_DONE;
                    }
                }

                if (auth && !m_config.password_hash.empty()) {
                    std::string auth_header = extract_header(raw_request, "Authorization");
                    std::string token;

                    if (auth_header.substr(0, 7) == "Bearer ") {
                        token = auth_header.substr(7);
                    }

                    if (token.empty() || !verify_auth_token(token)) {
                        LOG_WARN("Router", "Authentication failed for request to " + route_path);
                        raw_response = make_http_response(
                            error_response("Authentication required. Use POST /api/login to get a token.", 401).dump(), 401);
                        return rt::FLAG_DONE;
                    }
                }

                try {
                    json result = handler(req_json, headers);
                    raw_response = make_http_response(result.dump());
                } catch (const agent::BaseException& e) {
                    LOG_ERROR("Router", std::string("Handler error: ") + e.what());
                    raw_response = make_http_response(
                        error_response(std::string("[") + std::string(e.error_code_name()) + "] " + e.message()).dump(), 500);
                } catch (const std::exception& e) {
                    LOG_ERROR("Router", std::string("Handler exception: ") + e.what());
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
                auto headers = extract_all_headers(raw_request);

                std::string method;
                {
                    std::istringstream iss(raw_request);
                    iss >> method;
                }

                if (method == "OPTIONS" && m_config.enable_cors) {
                    write("HTTP/1.1 204 No Content\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
                          "\r\n");
                    return;
                }

                std::string body = extract_json_body(raw_request);

                json req_json;
                if (!body.empty()) {
                    try {
                        req_json = json::parse(body);
                    } catch (const json::parse_error& e) {
                        LOG_WARN("Router", std::string("Invalid JSON in stream request: ") + e.what());
                        write(make_http_response(
                            error_response("Invalid JSON").dump(), 400));
                        return;
                    }
                }

                if (auth && !m_config.password_hash.empty()) {
                    std::string auth_header = extract_header(raw_request, "Authorization");
                    std::string token;
                    if (auth_header.substr(0, 7) == "Bearer ") {
                        token = auth_header.substr(7);
                    }
                    if (token.empty() || !verify_auth_token(token)) {
                        LOG_WARN("Router", "Authentication failed for stream request");
                        write(make_http_response(
                            error_response("Authentication required", 401).dump(), 401));
                        return;
                    }
                }

                write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Connection: close\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "\r\n");

                try {
                    handler(req_json, headers, [&write](std::string_view chunk) {
                        std::string sse = "data: " + std::string(chunk) + "\n\n";
                        write(sse);
                    });
                    write("data: [DONE]\n\n");
                } catch (const agent::BaseException& e) {
                    LOG_ERROR("Router", std::string("Stream handler error: ") + e.what());
                    write("data: [ERROR] " + std::string(e.error_code_name()) + ": " + e.message() + "\n\n");
                } catch (const std::exception& e) {
                    LOG_ERROR("Router", std::string("Stream handler exception: ") + e.what());
                    write("data: [ERROR] " + std::string(e.what()) + "\n\n");
                }
            });
    }

    m_impl->worker = std::thread([this]() {
        try {
            m_impl->server->run(m_impl->ros);
        } catch (const std::exception& e) {
            LOG_ERROR("Router", std::string("Server exception: ") + e.what());
        }
    });

    m_running.store(true);
    LOG_INFO("Router", "Listening on " + m_config.bind_address + ":" + std::to_string(m_config.port));
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
    if (m_config.password_hash.empty()) return true;
    std::lock_guard lock(const_cast<std::mutex&>(m_token_mutex));
    return m_active_tokens.count(std::string(token)) > 0;
}

json Router::success_response(const json& data) {
    return {{"success", true}, {"data", data}};
}

json Router::error_response(std::string_view message, int code) {
    return {{"success", false}, {"error", message}, {"code", code}};
}

void Router::register_default_routes() {
    add_route({"GET", "/", [this](const json& req, const auto& headers) -> json {
        return success_response({{"service", "agent.cpp"}, {"version", AGENT_VERSION}});
    }, false});

    add_route({"GET", "/api/health", [this](const json& req, const auto& headers) -> json {
        auto& sys = ModuleRegistry::instance().require<System>();
        return success_response(sys.system_info());
    }, false});

    add_route({"POST", "/api/login", [this](const json& req, const auto& headers) -> json {
        if (!req.contains("password")) return error_response("password is required", 401);
        if (authenticate(req["password"].get<std::string>())) {
            auto token = generate_session_token();
            {
                std::lock_guard lock(m_token_mutex);
                m_active_tokens.insert(token);
            }
            return success_response({{"authenticated", true}, {"token", token}});
        }
        return error_response("Invalid password", 401);
    }, false});

    add_route({"POST", "/api/logout", [this](const json& req, const auto& headers) -> json {
        auto it = headers.find("authorization");
        if (it != headers.end()) {
            std::string auth = it->second;
            if (auth.substr(0, 7) == "Bearer ") {
                std::string token = auth.substr(7);
                std::lock_guard lock(m_token_mutex);
                m_active_tokens.erase(token);
            }
        }
        return success_response({{"logged_out", true}});
    }});

    add_route({"POST", "/api/input", [this](const json& req, const auto& headers) -> json {
        if (!req.contains("text")) return error_response("text is required");
        auto& agent = ModuleRegistry::instance().require<Agent>();
        auto result = agent.execute(req["text"].get<std::string>(), nullptr, false);
        return success_response({
            {"output", result.output},
            {"iterations", result.iterations_used},
            {"success", result.success}
        });
    }});

    add_stream_route({"POST", "/api/input/stream", [this](const json& req, const auto& headers, auto writer) {
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

    add_route({"GET", "/api/session", [this](const json& req, const auto& headers) -> json {
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

    add_route({"POST", "/api/session/new", [this](const json& req, const auto& headers) -> json {
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        auto& sys = ModuleRegistry::instance().require<System>();
        auto name = req.value("name", "");
        auto info = session.new_session(sys.project_dir().string(), name);
        return success_response({{"id", info.id}, {"name", info.name}});
    }});

    add_route({"POST", "/api/session/delete", [this](const json& req, const auto& headers) -> json {
        if (!req.contains("id")) return error_response("id is required");
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        session.delete_session(req["id"].get<std::string>());
        return success_response();
    }});

    add_route({"POST", "/api/session/switch", [this](const json& req, const auto& headers) -> json {
        if (!req.contains("id")) return error_response("id is required");
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        session.set_current(req["id"].get<std::string>());
        return success_response({{"current", req["id"]}});
    }});

    add_route({"GET", "/api/models", [this](const json& req, const auto& headers) -> json {
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

    add_route({"POST", "/api/models/switch", [this](const json& req, const auto& headers) -> json {
        auto& provider = ModuleRegistry::instance().require<Provider>();
        if (req.contains("provider")) provider.set_current(req["provider"].get<std::string>());
        if (req.contains("model")) provider.set_model(req["model"].get<std::string>());
        return success_response({{"current_model", provider.current_model().name}});
    }});

    add_route({"GET", "/api/tools", [this](const json& req, const auto& headers) -> json {
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

    add_route({"GET", "/api/memory", [this](const json& req, const auto& headers) -> json {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        auto all = mem.list_all();
        json result = json::array();
        for (auto& m : all) {
            result.push_back({
                {"id", m.id}, {"title", m.title},
                {"category", m.category}, {"importance", m.importance}
            });
        }
        return success_response(result);
    }});

    add_route({"POST", "/api/memory/search", [this](const json& req, const auto& headers) -> json {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        auto query = req.value("query", "");
        auto results = mem.search_text(query, req.value("max_results", 5));
        json arr = json::array();
        for (auto& r : results) {
            arr.push_back({
                {"id", r.id}, {"title", r.title},
                {"content", r.content}, {"category", r.category}
            });
        }
        return success_response(arr);
    }});

    add_route({"GET", "/api/config", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        json providers = json::array();
        for (auto& p : cfg.providers) {
            providers.push_back({
                {"name", p.name}, {"type", p.type},
                {"api_base", p.api_base}, {"model", p.model}
            });
        }
        return success_response({
            {"default_provider", cfg.default_provider},
            {"default_model", cfg.default_model},
            {"providers", providers},
            {"max_context_tokens", cfg.max_context_tokens},
            {"stream_output", cfg.stream_output}
        });
    }});
}

} // namespace agent
#endif
