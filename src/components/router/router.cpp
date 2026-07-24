#ifdef AGENT_ENABLE_ROUTER
#include "components/router/router.hpp"
#include "components/session/session.hpp"
#include "components/agent/agent.hpp"
#include "components/system/system.hpp"
#include "components/permission/permission.hpp"
#include "components/memory/memory.hpp"
#include "components/edit_history/edit_history.hpp"
#include "components/notice/notice.hpp"
#include "components/provider/provider.hpp"
#include "core/logger.hpp"
#include "core/exception.hpp"
#include "utils/crypto.hpp"

#include "external/servic/servic.hpp"

#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <set>

namespace agent {

using json = nlohmann::json;
namespace asio = boost::asio;
namespace fs = std::filesystem;

static std::string get_mime_type(const std::string& path) {
    static const std::map<std::string, std::string> mime_types = {
        {".html", "text/html"},
        {".css", "text/css"},
        {".js", "application/javascript"},
        {".json", "application/json"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".ttf", "font/ttf"},
        {".eot", "application/vnd.ms-fontobject"},
        {".otf", "font/otf"},
        {".txt", "text/plain"},
        {".xml", "application/xml"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
        {".mp3", "audio/mpeg"},
        {".mp4", "video/mp4"},
        {".webm", "video/webm"},
        {".webp", "image/webp"},
    };

    size_t dot_pos = path.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string ext = path.substr(dot_pos);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        auto it = mime_types.find(ext);
        if (it != mime_types.end()) {
            return it->second;
        }
    }
    return "application/octet-stream";
}

static std::string read_file_content(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

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

#ifdef AGENT_ENABLE_TLS
    if (m_config.enable_tls) {
        if (m_config.cert_path.empty() || m_config.key_path.empty()) {
            LOG_ERROR("Router", "TLS enabled but cert_path or key_path not configured");
            throw BaseException("TLS configuration incomplete: cert_path and key_path required");
        }
        m_impl->server->enable_tls(m_config.cert_path, m_config.key_path);
        LOG_INFO("Router", "TLS enabled with cert: " + m_config.cert_path);
    }
#endif

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

                    // Check if this is a static file request
                    if (result.contains("_serve_static") && result["_serve_static"].get<bool>()) {
                        std::string file_path;
                        std::string content_type = "text/html";

                        if (result.contains("_file")) {
                            // Direct file request (e.g., webui.html)
                            file_path = result["_file"].get<std::string>();
                            if (result.contains("_content_type")) {
                                content_type = result["_content_type"].get<std::string>();
                            }
                        } else if (result.contains("_static_dir")) {
                            // Static directory request (e.g., webui/*)
                            std::string url;
                            {
                                std::istringstream iss(raw_request);
                                iss >> url;
                            }
                            // Remove leading /webui/ to get relative path
                            if (url.substr(0, 7) == "/webui/") {
                                file_path = "webui/" + url.substr(7);
                            } else {
                                file_path = "webui/" + url;
                            }
                            content_type = get_mime_type(file_path);
                        }

                        // Try to read the file
                        std::string content = read_file_content(file_path);
                        if (content.empty()) {
                            raw_response = make_http_response("File not found", 404, "text/plain");
                        } else {
                            raw_response = make_http_response(content, 200, content_type);
                        }
                        return rt::FLAG_DONE;
                    }

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
    // Serve webui/index.html at root
    add_route({"GET", "/", [this](const json& req, const auto& headers) -> json {
        // This will be handled specially in start() to return HTML
        return {{"_serve_static", true}, {"_file", "webui/index.html"}, {"_content_type", "text/html"}};
    }, false});

    // Serve static files from webui/ directory
    add_route({"GET", "/webui/*", [this](const json& req, const auto& headers) -> json {
        return {{"_serve_static", true}, {"_static_dir", "webui"}};
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

    // ── Settings API ─────────────────────────────────────────────
    add_route({"GET", "/api/settings", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        return success_response({
            {"user_name", cfg.user_name},
            {"agent_name", cfg.agent_name},
            {"default_model", cfg.default_model},
            {"default_provider", cfg.default_provider},
            {"max_context_tokens", cfg.max_context_tokens},
            {"max_mpc_rounds", cfg.max_mpc_rounds},
            {"request_timeout_sec", cfg.request_timeout_sec},
            {"stream_output", cfg.stream_output},
            {"auto_memory", cfg.auto_memory},
            {"router_enabled", cfg.router_enabled},
            {"router_port", cfg.router_port},
            {"router_bind", cfg.router_bind},
            {"router_tls", cfg.router_tls},
            {"router_cert_path", cfg.router_cert_path},
            {"router_key_path", cfg.router_key_path},
            {"default_tool_permission", cfg.default_tool_permission},
            {"websearch_proxy", cfg.websearch_proxy},
            {"webfetch_proxy", cfg.webfetch_proxy},
            {"webfetch_max_size_mb", cfg.webfetch_max_size_mb},
            {"webfetch_timeout_sec", cfg.webfetch_timeout_sec}
        });
    }});

    add_route({"PUT", "/api/settings", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        if (req.contains("user_name")) cfg.user_name = req["user_name"].get<std::string>();
        if (req.contains("agent_name")) cfg.agent_name = req["agent_name"].get<std::string>();
        if (req.contains("default_model")) cfg.default_model = req["default_model"].get<std::string>();
        if (req.contains("default_provider")) cfg.default_provider = req["default_provider"].get<std::string>();
        if (req.contains("max_context_tokens")) cfg.max_context_tokens = req["max_context_tokens"].get<int>();
        if (req.contains("max_mpc_rounds")) cfg.max_mpc_rounds = req["max_mpc_rounds"].get<int>();
        if (req.contains("request_timeout_sec")) cfg.request_timeout_sec = req["request_timeout_sec"].get<int>();
        if (req.contains("stream_output")) cfg.stream_output = req["stream_output"].get<bool>();
        if (req.contains("auto_memory")) cfg.auto_memory = req["auto_memory"].get<bool>();
        if (req.contains("default_tool_permission")) cfg.default_tool_permission = req["default_tool_permission"].get<std::string>();
        if (req.contains("websearch_proxy")) cfg.websearch_proxy = req["websearch_proxy"].get<std::string>();
        if (req.contains("webfetch_proxy")) cfg.webfetch_proxy = req["webfetch_proxy"].get<std::string>();
        if (req.contains("webfetch_max_size_mb")) cfg.webfetch_max_size_mb = req["webfetch_max_size_mb"].get<int>();
        if (req.contains("webfetch_timeout_sec")) cfg.webfetch_timeout_sec = req["webfetch_timeout_sec"].get<int>();
        cfg.save();
        return success_response({{"message", "Settings saved"}});
    }});

    // ── Router/TLS Settings API ──────────────────────────────────
    add_route({"PUT", "/api/settings/router", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        if (req.contains("router_port")) cfg.router_port = req["router_port"].get<int>();
        if (req.contains("router_bind")) cfg.router_bind = req["router_bind"].get<std::string>();
        if (req.contains("router_tls")) cfg.router_tls = req["router_tls"].get<bool>();
        if (req.contains("router_cert_path")) cfg.router_cert_path = req["router_cert_path"].get<std::string>();
        if (req.contains("router_key_path")) cfg.router_key_path = req["router_key_path"].get<std::string>();
        if (req.contains("password")) {
            std::string pwd = req["password"].get<std::string>();
            if (!pwd.empty()) {
                set_password(pwd);
                cfg.router_password_hash = m_config.password_hash;
            }
        }
        cfg.save();
        return success_response({{"message", "Router settings saved. Restart required to apply changes."}});
    }});

    // ── Providers API ────────────────────────────────────────────
    add_route({"GET", "/api/providers", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        json providers = json::array();
        for (auto& p : cfg.providers) {
            providers.push_back({
                {"name", p.name}, {"type", p.type},
                {"api_base", p.api_base}, {"model", p.model},
                {"context_length", p.context_length},
                {"temperature", p.temperature},
                {"top_p", p.top_p},
                {"max_tokens", p.max_tokens},
                {"thinking_mode", p.thinking_mode},
                {"thinking_budget", p.thinking_budget},
                {"supports_vision", p.supports_vision},
                {"supports_tools", p.supports_tools}
            });
        }
        return success_response(providers);
    }});

    add_route({"POST", "/api/providers", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        Config::ProviderConfig p;
        p.name = req.value("name", "");
        p.type = req.value("type", "openai");
        p.api_base = req.value("api_base", "");
        p.api_key = req.value("api_key", "");
        p.model = req.value("model", "");
        p.context_length = req.value("context_length", 128000);
        p.temperature = req.value("temperature", 0.7f);
        p.top_p = req.value("top_p", 1.0f);
        p.max_tokens = req.value("max_tokens", 4096);
        p.thinking_mode = req.value("thinking_mode", "");
        p.thinking_budget = req.value("thinking_budget", 16000);
        p.supports_vision = req.value("supports_vision", false);
        p.supports_tools = req.value("supports_tools", true);
        if (p.name.empty()) return error_response("name is required");
        cfg.providers.push_back(p);
        cfg.save();
        return success_response({{"message", "Provider added"}, {"name", p.name}});
    }});

    add_route({"PUT", "/api/providers/:name", [this](const json& req, const auto& headers) -> json {
        // Note: dynamic params handled via query string or body
        auto& cfg = Config::instance();
        std::string name = req.value("name", "");
        if (name.empty()) return error_response("name is required");
        for (auto& p : cfg.providers) {
            if (p.name == name) {
                if (req.contains("type")) p.type = req["type"].get<std::string>();
                if (req.contains("api_base")) p.api_base = req["api_base"].get<std::string>();
                if (req.contains("api_key")) p.api_key = req["api_key"].get<std::string>();
                if (req.contains("model")) p.model = req["model"].get<std::string>();
                if (req.contains("context_length")) p.context_length = req["context_length"].get<int>();
                if (req.contains("temperature")) p.temperature = req["temperature"].get<float>();
                if (req.contains("top_p")) p.top_p = req["top_p"].get<float>();
                if (req.contains("max_tokens")) p.max_tokens = req["max_tokens"].get<int>();
                if (req.contains("supports_vision")) p.supports_vision = req["supports_vision"].get<bool>();
                if (req.contains("supports_tools")) p.supports_tools = req["supports_tools"].get<bool>();
                cfg.save();
                return success_response({{"message", "Provider updated"}});
            }
        }
        return error_response("Provider not found", 404);
    }});

    add_route({"DELETE", "/api/providers", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        std::string name = req.value("name", "");
        if (name.empty()) return error_response("name is required");
        auto it = std::remove_if(cfg.providers.begin(), cfg.providers.end(),
            [&](const Config::ProviderConfig& p) { return p.name == name; });
        if (it == cfg.providers.end()) return error_response("Provider not found", 404);
        cfg.providers.erase(it, cfg.providers.end());
        cfg.save();
        return success_response({{"message", "Provider deleted"}});
    }});

    // ── Preset Models API ────────────────────────────────────────
    add_route({"GET", "/api/providers/presets", [this](const json& req, const auto& headers) -> json {
        json presets = json::array();
        presets.push_back({
            {"name", "OpenAI"},
            {"type", "openai"},
            {"api_base", "https://api.openai.com/v1"},
            {"models", json::array({"gpt-4o", "gpt-4o-mini", "gpt-4-turbo", "gpt-3.5-turbo", "o1-preview", "o1-mini"})}
        });
        presets.push_back({
            {"name", "Anthropic"},
            {"type", "anthropic"},
            {"api_base", "https://api.anthropic.com"},
            {"models", json::array({"claude-3-5-sonnet-20241022", "claude-3-opus-20240229", "claude-3-haiku-20240307"})}
        });
        presets.push_back({
            {"name", "Ollama (Local)"},
            {"type", "ollama"},
            {"api_base", "http://localhost:11434"},
            {"models", json::array({"llama3.1", "llama3", "mistral", "codellama", "phi3"})}
        });
        presets.push_back({
            {"name", "LLaMA Server (Local)"},
            {"type", "llama_server"},
            {"api_base", "http://localhost:8080"},
            {"models", json::array({"default"})}
        });
        presets.push_back({
            {"name", "OpenRouter"},
            {"type", "openai"},
            {"api_base", "https://openrouter.ai/api/v1"},
            {"models", json::array({"anthropic/claude-3.5-sonnet", "google/gemini-pro-1.5", "meta-llama/llama-3.1-405b-instruct"})}
        });
        presets.push_back({
            {"name", "Groq"},
            {"type", "openai"},
            {"api_base", "https://api.groq.com/openai/v1"},
            {"models", json::array({"llama-3.1-70b-versatile", "llama-3.1-8b-instant", "mixtral-8x7b-32768"})}
        });
        return success_response(presets);
    }});

    // ── Permissions API ──────────────────────────────────────────
    add_route({"GET", "/api/permissions", [this](const json& req, const auto& headers) -> json {
        auto& perm = ModuleRegistry::instance().require<Permission>();
        auto rules = perm.list_rules();
        json result = json::array();
        for (auto& r : rules) {
            std::string level_str;
            switch (r.level) {
                case PermissionLevel::Auto: level_str = "auto"; break;
                case PermissionLevel::Ask: level_str = "ask"; break;
                case PermissionLevel::Deny: level_str = "deny"; break;
            }
            result.push_back({
                {"tool_name", r.tool_name},
                {"level", level_str},
                {"description", r.description}
            });
        }
        json dangerous = json::array();
        for (auto& p : perm.list_dangerous_patterns()) {
            dangerous.push_back(p);
        }
        return success_response({
            {"rules", result},
            {"dangerous_patterns", dangerous}
        });
    }});

    add_route({"PUT", "/api/permissions", [this](const json& req, const auto& headers) -> json {
        auto& perm = ModuleRegistry::instance().require<Permission>();
        if (req.contains("tool_name") && req.contains("level")) {
            std::string tool = req["tool_name"].get<std::string>();
            std::string level = req["level"].get<std::string>();
            PermissionLevel pl;
            if (level == "auto") pl = PermissionLevel::Auto;
            else if (level == "deny") pl = PermissionLevel::Deny;
            else pl = PermissionLevel::Ask;
            perm.set_tool_permission(tool, pl);
        }
        if (req.contains("default_level")) {
            std::string level = req["default_level"].get<std::string>();
            PermissionLevel pl;
            if (level == "auto") pl = PermissionLevel::Auto;
            else if (level == "deny") pl = PermissionLevel::Deny;
            else pl = PermissionLevel::Ask;
            perm.set_default_permission(pl);
        }
        return success_response({{"message", "Permissions updated"}});
    }});

    // ── Channels API ─────────────────────────────────────────────
    add_route({"GET", "/api/channels", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        json channels = json::array();
        for (auto& ch : cfg.channels) {
            channels.push_back({
                {"type", ch.type},
                {"enabled", ch.enabled},
                {"has_token", !ch.token.empty()},
                {"proxy", ch.proxy}
            });
        }
        return success_response(channels);
    }});

    add_route({"POST", "/api/channels", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        Config::ChannelConfig ch;
        ch.type = req.value("type", "");
        ch.token = req.value("token", "");
        ch.proxy = req.value("proxy", "");
        ch.enabled = req.value("enabled", false);
        if (ch.type.empty()) return error_response("type is required");
        cfg.channels.push_back(ch);
        cfg.save();
        return success_response({{"message", "Channel added"}});
    }});

    add_route({"PUT", "/api/channels", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        std::string type = req.value("type", "");
        for (auto& ch : cfg.channels) {
            if (ch.type == type) {
                if (req.contains("token")) ch.token = req["token"].get<std::string>();
                if (req.contains("proxy")) ch.proxy = req["proxy"].get<std::string>();
                if (req.contains("enabled")) ch.enabled = req["enabled"].get<bool>();
                cfg.save();
                return success_response({{"message", "Channel updated"}});
            }
        }
        return error_response("Channel not found", 404);
    }});

    add_route({"DELETE", "/api/channels", [this](const json& req, const auto& headers) -> json {
        auto& cfg = Config::instance();
        std::string type = req.value("type", "");
        auto it = std::remove_if(cfg.channels.begin(), cfg.channels.end(),
            [&](const Config::ChannelConfig& c) { return c.type == type; });
        if (it == cfg.channels.end()) return error_response("Channel not found", 404);
        cfg.channels.erase(it, cfg.channels.end());
        cfg.save();
        return success_response({{"message", "Channel deleted"}});
    }});

    // ── Edit History API ─────────────────────────────────────────
    add_route({"GET", "/api/edit-history", [this](const json& req, const auto& headers) -> json {
        auto& history = ModuleRegistry::instance().require<EditHistory>();
        auto records = history.list_records(req.value("limit", 100));
        json result = json::array();
        for (auto& r : records) {
            result.push_back({
                {"tool_name", r.tool_name},
                {"file_path", r.file_path},
                {"diff", r.diff},
                {"timestamp", r.timestamp},
                {"session_id", r.session_id},
                {"before", r.before},
                {"after", r.after}
            });
        }
        return success_response(result);
    }});

    add_route({"GET", "/api/edit-history/session", [this](const json& req, const auto& headers) -> json {
        auto& history = ModuleRegistry::instance().require<EditHistory>();
        std::string session_id = req.value("session_id", "");
        auto records = history.list_by_session(session_id);
        json result = json::array();
        for (auto& r : records) {
            result.push_back({
                {"tool_name", r.tool_name},
                {"file_path", r.file_path},
                {"diff", r.diff},
                {"timestamp", r.timestamp},
                {"session_id", r.session_id}
            });
        }
        return success_response(result);
    }});

    add_route({"GET", "/api/edit-history/file", [this](const json& req, const auto& headers) -> json {
        auto& history = ModuleRegistry::instance().require<EditHistory>();
        std::string file_path = req.value("file_path", "");
        auto records = history.list_by_file(file_path);
        json result = json::array();
        for (auto& r : records) {
            result.push_back({
                {"tool_name", r.tool_name},
                {"file_path", r.file_path},
                {"diff", r.diff},
                {"timestamp", r.timestamp},
                {"session_id", r.session_id}
            });
        }
        return success_response(result);
    }});

    add_route({"POST", "/api/edit-history/rollback", [this](const json& req, const auto& headers) -> json {
        auto& history = ModuleRegistry::instance().require<EditHistory>();
        size_t index = req.value("index", 0);
        if (history.rollback_to_record(index)) {
            return success_response({{"message", "Rollback successful"}});
        }
        return error_response("Rollback failed");
    }});

    // ── Session Messages API ─────────────────────────────────────
    add_route({"GET", "/api/session/messages", [this](const json& req, const auto& headers) -> json {
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        std::string session_id = req.value("session_id", "");
        if (session_id.empty()) session_id = session.current_session().id;
        auto messages = session.get_messages(session_id);
        json result = json::array();
        for (auto& m : messages) {
            json msg = {
                {"role", m.role},
                {"content", m.content},
                {"timestamp", m.timestamp}
            };
            if (!m.tool_call_id.empty()) msg["tool_call_id"] = m.tool_call_id;
            if (!m.tool_name.empty()) msg["tool_name"] = m.tool_name;
            if (!m.metadata.empty()) msg["metadata"] = m.metadata;
            result.push_back(msg);
        }
        return success_response(result);
    }});

    add_route({"POST", "/api/session/rename", [this](const json& req, const auto& headers) -> json {
        auto& session = ModuleRegistry::instance().require<SessionManager>();
        std::string id = req.value("id", "");
        std::string name = req.value("name", "");
        if (id.empty()) return error_response("id is required");
        session.rename_session(id, name);
        return success_response({{"message", "Session renamed"}});
    }});

    // ── Memory CRUD API ──────────────────────────────────────────
    add_route({"POST", "/api/memory", [this](const json& req, const auto& headers) -> json {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        MemoryEntry entry;
        entry.title = req.value("title", "");
        entry.content = req.value("content", "");
        entry.category = req.value("category", "general");
        entry.importance = req.value("importance", 0.5);
        if (req.contains("keywords")) {
            for (auto& k : req["keywords"]) entry.keywords.push_back(k.get<std::string>());
        }
        auto saved = mem.save_entry(entry);
        return success_response({{"id", saved.id}, {"message", "Memory saved"}});
    }});

    add_route({"PUT", "/api/memory", [this](const json& req, const auto& headers) -> json {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        MemoryEntry entry;
        entry.id = req.value("id", "");
        entry.title = req.value("title", "");
        entry.content = req.value("content", "");
        entry.category = req.value("category", "general");
        entry.importance = req.value("importance", 0.5);
        if (req.contains("keywords")) {
            for (auto& k : req["keywords"]) entry.keywords.push_back(k.get<std::string>());
        }
        mem.update_entry(entry);
        return success_response({{"message", "Memory updated"}});
    }});

    add_route({"DELETE", "/api/memory", [this](const json& req, const auto& headers) -> json {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        std::string id = req.value("id", "");
        if (id.empty()) return error_response("id is required");
        mem.delete_entry(id);
        return success_response({{"message", "Memory deleted"}});
    }});

    add_route({"GET", "/api/memory/categories", [this](const json& req, const auto& headers) -> json {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        auto all = mem.list_all();
        std::set<std::string> categories;
        for (auto& m : all) categories.insert(m.category);
        json result = json::array();
        for (auto& c : categories) result.push_back(c);
        return success_response(result);
    }});

    // ── System Info API ──────────────────────────────────────────
    add_route({"GET", "/api/system/info", [this](const json& req, const auto& headers) -> json {
        auto& sys = ModuleRegistry::instance().require<System>();
        return success_response(sys.system_info());
    }});

    add_route({"GET", "/api/system/commands", [this](const json& req, const auto& headers) -> json {
        auto& sys = ModuleRegistry::instance().require<System>();
        auto commands = sys.list_commands();
        json result = json::array();
        for (auto& cmd : commands) {
            result.push_back({
                {"name", cmd.name},
                {"description", cmd.description},
                {"usage", cmd.usage},
                {"aliases", cmd.aliases}
            });
        }
        return success_response(result);
    }});

    add_route({"POST", "/api/system/restart", [this](const json& req, const auto& headers) -> json {
        auto& sys = ModuleRegistry::instance().require<System>();
        sys.restart();
        return success_response({{"message", "System restarting"}});
    }});

    // ── Notice/Notification API ──────────────────────────────────
    add_route({"GET", "/api/notices", [this](const json& req, const auto& headers) -> json {
        auto& notice = ModuleRegistry::instance().require<Notice>();
        auto events = notice.recent_events(req.value("count", 50));
        json result = json::array();
        for (auto& e : events) {
            std::string type_str;
            switch (e.type) {
                case NoticeEvent::Type::Info: type_str = "info"; break;
                case NoticeEvent::Type::Warning: type_str = "warning"; break;
                case NoticeEvent::Type::Error: type_str = "error"; break;
                case NoticeEvent::Type::BackgroundTaskCompleted: type_str = "task_completed"; break;
                case NoticeEvent::Type::ContextThreshold: type_str = "context_threshold"; break;
                case NoticeEvent::Type::SystemStatus: type_str = "system_status"; break;
                case NoticeEvent::Type::PermissionRequest: type_str = "permission_request"; break;
            }
            result.push_back({
                {"type", type_str},
                {"title", e.title},
                {"message", e.message},
                {"source", e.source},
                {"action_required", e.action_required}
            });
        }
        return success_response(result);
    }});

    // ── Tools API ────────────────────────────────────────────────
    add_route({"GET", "/api/tools/categories", [this](const json& req, const auto& headers) -> json {
        auto& tools = ModuleRegistry::instance().require<Tools>();
        auto list = tools.list_tools();
        std::set<std::string> categories;
        for (auto& t : list) {
            for (auto& c : t.categories) categories.insert(c);
        }
        json result = json::array();
        for (auto& c : categories) result.push_back(c);
        return success_response(result);
    }});

    // ── SSE Notification Stream ──────────────────────────────────
    add_stream_route({"GET", "/api/events", [this](const json& req, const auto& headers, auto writer) {
        json init = {{"type", "connected"}, {"message", "SSE stream connected"}};
        writer(init.dump());
    }, false});
}

} // namespace agent
#endif
