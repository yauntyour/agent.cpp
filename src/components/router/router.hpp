#pragma once
#ifdef AGENT_ENABLE_ROUTER

#include "core/module.hpp"
#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace agent {

using RouteHandler = std::function<nlohmann::json(const nlohmann::json& request)>;
using StreamHandler = std::function<void(const nlohmann::json& request,
                                          std::function<void(std::string_view chunk)> writer)>;

struct Route {
    std::string method;  // "GET", "POST", "PUT", "DELETE"
    std::string path;    // e.g. "/api/input"
    RouteHandler handler;
    bool requires_auth = true;
};

struct StreamRoute {
    std::string method;
    std::string path;
    StreamHandler handler;
    bool requires_auth = true;
};

struct RouterConfig {
    int port = 18080;
    std::string bind_address = "127.0.0.1";
    bool enable_tls = false;
    std::string cert_path;
    std::string key_path;
    std::string password_hash;  // argon2id hash for auth
    bool enable_cors = true;
    int max_connections = 100;
};

class Router : public Module<Router> {
public:
    static constexpr std::string_view static_name() { return "router"; }

    void on_initialize() override;
    void on_shutdown() override;

    // ── Configuration ──────────────────────────────────────────
    void configure(const RouterConfig& config);
    RouterConfig get_config() const;

    // ── Route registration ─────────────────────────────────────
    void add_route(const Route& route);
    void add_stream_route(const StreamRoute& route);
    void clear_routes();

    // ── Server control ─────────────────────────────────────────
    void start();
    void stop();
    void restart();
    bool is_running() const;

    // ── Auth ───────────────────────────────────────────────────
    bool authenticate(std::string_view password) const;
    void set_password(std::string_view password);
    bool verify_auth_token(std::string_view token) const;

    // ── API helper ─────────────────────────────────────────────
    static nlohmann::json success_response(const nlohmann::json& data = {});
    static nlohmann::json error_response(std::string_view message, int code = 400);

private:
    void register_default_routes();

    RouterConfig m_config;
    std::vector<Route> m_routes;
    std::vector<StreamRoute> m_stream_routes;
    std::atomic<bool> m_running{false};

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace agent

#endif // AGENT_ENABLE_ROUTER
