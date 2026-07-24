#ifdef AGENT_ENABLE_ROUTER
#include "components/router/router.hpp"
#include "components/session/session.hpp"
#include "components/agent/agent.hpp"
#include "components/system/system.hpp"
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

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
    m_running.store(true);
    // servic.cpp integration: start HTTP server on m_config.port
}

void Router::stop() {
    m_running.store(false);
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
    // Simple token verification — full implementation uses JWT or session tokens
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
