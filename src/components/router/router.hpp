#pragma once
#ifdef AGENT_ENABLE_ROUTER

#include "core/module.hpp"
#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>

namespace agent {

using RouteHandler = std::function<nlohmann::json(
    const nlohmann::json& request,
    const std::map<std::string, std::string>& headers)>;

using StreamHandler = std::function<void(
    const nlohmann::json& request,
    const std::map<std::string, std::string>& headers,
    std::function<void(std::string_view chunk)> writer)>;

struct Route {
    std::string method;
    std::string path;
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
    std::string password_hash;
    bool enable_cors = true;
    int max_connections = 100;
};

struct RouterImpl;

class Router : public Module<Router> {
public:
    static constexpr std::string_view static_name() { return "router"; }

    Router();
    ~Router() override;

    void on_initialize();
    void on_shutdown();

    void configure(const RouterConfig& config);
    RouterConfig get_config() const;

    void add_route(const Route& route);
    void add_stream_route(const StreamRoute& route);
    void clear_routes();

    void start();
    void stop();
    void restart();
    bool is_running() const;

    bool authenticate(std::string_view password) const;
    void set_password(std::string_view password);
    bool verify_auth_token(std::string_view token) const;

    static nlohmann::json success_response(const nlohmann::json& data = {});
    static nlohmann::json error_response(std::string_view message, int code = 400);

private:
    void register_default_routes();
    std::string generate_session_token();
    std::string extract_header(const std::string& raw_request, std::string_view header_name);
    std::map<std::string, std::string> extract_all_headers(const std::string& raw_request);

    RouterConfig m_config;
    std::vector<Route> m_routes;
    std::vector<StreamRoute> m_stream_routes;
    std::atomic<bool> m_running{false};

    std::set<std::string> m_active_tokens;
    std::mutex m_token_mutex;

    std::unique_ptr<RouterImpl> m_impl;
};

} // namespace agent

#endif
