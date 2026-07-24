#include "core/config.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace agent {

using json = nlohmann::json;

Config& Config::instance() {
    static Config config;
    return config;
}

fs::path Config::exe_dir() const {
    static fs::path dir = []() {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return fs::path(path).parent_path();
#else
        return fs::read_symlink("/proc/self/exe").parent_path();
#endif
    }();
    return dir;
}

fs::path Config::agent_dir() const {
    auto dir = exe_dir() / ".agent";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

fs::path Config::project_dir() const {
    return m_project_dir.empty() ? fs::current_path() : m_project_dir;
}

void Config::set_project_dir(const fs::path& dir) {
    std::lock_guard lock(m_mutex);
    m_project_dir = fs::absolute(dir);
    auto proj_agent = m_project_dir / ".agent";
    if (!fs::exists(proj_agent)) fs::create_directories(proj_agent);
}

fs::path Config::session_dir() const {
    auto dir = project_dir() / ".agent" / "sessions";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

fs::path Config::memory_dir() const {
    auto dir = agent_dir() / "memory";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

fs::path Config::edit_history_dir() const {
    auto dir = project_dir() / ".agent" / "edit-history";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

fs::path Config::assets_dir() const {
    auto dir = project_dir() / ".agent" / "sessions" / "assets";
    if (!fs::exists(dir)) fs::create_directories(dir);
    return dir;
}

fs::path Config::keychain_path() const {
    return agent_dir() / "keychain";
}

fs::path Config::config_path() const {
    return agent_dir() / "config.json";
}

void Config::load() {
    auto path = config_path();
    if (!fs::exists(path)) {
        save();
        return;
    }
    try {
        std::ifstream f(path);
        json j;
        f >> j;
        j.get_to(*this);
        LOG_INFO("Config", "Configuration loaded from: " + path.string());
    } catch (const json::parse_error& e) {
        LOG_WARN("Config", "Corrupted config file, resetting to defaults: " + std::string(e.what()));
        save();
    } catch (const std::exception& e) {
        LOG_WARN("Config", "Failed to load config, resetting to defaults: " + std::string(e.what()));
        save();
    }
}

void Config::save() const {
    std::lock_guard lock(m_mutex);
    auto dir = agent_dir();
    if (!fs::exists(dir)) fs::create_directories(dir);
    json j;
    to_json(j, *this);
    std::ofstream f(config_path());
    f << j.dump(2);
}

void Config::reset() {
    std::lock_guard lock(m_mutex);
    user_name = "User";
    agent_name = "Agent";
    default_model = "gpt-4o";
    default_provider = "llama-server";
    max_context_tokens = 128000;
    max_mpc_rounds = 30;
    request_timeout_sec = 120;
    stream_output = true;
    auto_memory = true;
    tui_enabled = true;
    tui_theme = "default";
    themes.clear();
    themes["solarized"] = {
        "#268bd2", "#2aa198", "#859900", "#b58900", "#dc322f",
        "#002b36", "#839496", "#073642", "#073642", "#586e75"
    };
    themes["monokai"] = {
        "#f92672", "#a6e22e", "#e6db74", "#fd971f", "#f92672",
        "#272822", "#f8f8f2", "#3e3d32", "#3e3d32", "#75715e"
    };
    themes["dracula"] = {
        "#bd93f9", "#50fa7b", "#f1fa8c", "#ffb86c", "#ff5555",
        "#282a36", "#f8f8f2", "#44475a", "#44475a", "#6272a4"
    };
    router_enabled = false;
    router_port = 18080;
    router_bind = "127.0.0.1";
    router_tls = false;
    router_cert_path.clear();
    router_key_path.clear();
    router_password_hash.clear();
    providers.clear();
    channels.clear();
    auto_allowed_tools.clear();
    dangerous_commands = {
        "rm -rf /", "rm -rf /*", "rmdir /s", "del /f /q",
        "shutdown", "reboot", "halt", "mkfs", "dd if=",
        "fdisk", "format", ":(){ :|:& };:",
        "chmod 777", "sudo", "su -", "passwd",
        "git push --force", "git push -f",
        "git reset --hard", "git clean -fdx"
    };
    default_tool_permission = "ask";
    agents.clear();
    websearch_proxy.clear();
    websearch_user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
    bing_api_key.clear();
    google_api_key.clear();
    google_cse_id.clear();
    webfetch_proxy.clear();
    webfetch_max_size_mb = 50;
    webfetch_timeout_sec = 60;
    lsp_servers.clear();
    mcp_servers.clear();
    recent_projects.clear();
    max_recent_projects = 20;
    m_project_dir.clear();
}

void to_json(json& j, const Config& c) {
    j = json{
        {"user_name", c.user_name},
        {"agent_name", c.agent_name},
        {"default_model", c.default_model},
        {"default_provider", c.default_provider},
        {"max_context_tokens", c.max_context_tokens},
        {"max_mpc_rounds", c.max_mpc_rounds},
        {"request_timeout_sec", c.request_timeout_sec},
        {"stream_output", c.stream_output},
        {"auto_memory", c.auto_memory},
        {"tui_enabled", c.tui_enabled},
        {"tui_theme", c.tui_theme},
        {"router_enabled", c.router_enabled},
        {"router_port", c.router_port},
        {"router_bind", c.router_bind},
        {"router_tls", c.router_tls},
        {"dangerous_commands", c.dangerous_commands},
        {"default_tool_permission", c.default_tool_permission},
        {"websearch_proxy", c.websearch_proxy},
        {"websearch_user_agent", c.websearch_user_agent},
        {"webfetch_proxy", c.webfetch_proxy},
        {"webfetch_max_size_mb", c.webfetch_max_size_mb},
        {"webfetch_timeout_sec", c.webfetch_timeout_sec},
        {"recent_projects", c.recent_projects},
        {"max_recent_projects", c.max_recent_projects},
    };

    json themes = json::object();
    for (auto& [name, t] : c.themes) {
        themes[name] = {
            {"primary_color", t.primary_color},
            {"secondary_color", t.secondary_color},
            {"success_color", t.success_color},
            {"warning_color", t.warning_color},
            {"error_color", t.error_color},
            {"background_color", t.background_color},
            {"foreground_color", t.foreground_color},
            {"status_bar_color", t.status_bar_color},
            {"input_color", t.input_color},
            {"border_color", t.border_color}
        };
    }
    j["themes"] = themes;

    json providers = json::array();
    for (auto& p : c.providers) {
        providers.push_back({
            {"name", p.name}, {"type", p.type},
            {"api_base", p.api_base}, {"api_key", p.api_key},
            {"model", p.model}, {"context_length", p.context_length},
            {"temperature", p.temperature}, {"top_p", p.top_p},
            {"max_tokens", p.max_tokens}, {"thinking_mode", p.thinking_mode},
            {"thinking_budget", p.thinking_budget},
            {"supports_vision", p.supports_vision},
            {"supports_tools", p.supports_tools}
        });
    }
    j["providers"] = providers;

    json channels = json::array();
    for (auto& ch : c.channels) {
        channels.push_back({
            {"type", ch.type}, {"token", ch.token},
            {"proxy", ch.proxy}, {"enabled", ch.enabled}
        });
    }
    j["channels"] = channels;

    json agents = json::array();
    for (auto& a : c.agents) {
        agents.push_back({
            {"name", a.name}, {"system_prompt", a.system_prompt},
            {"allowed_tools", a.allowed_tools},
            {"allow_all_tools", a.allow_all_tools},
            {"max_sub_agents", a.max_sub_agents}
        });
    }
    j["agents"] = agents;

    json lsp = json::array();
    for (auto& l : c.lsp_servers) {
        lsp.push_back({
            {"id", l.id}, {"language", l.language},
            {"command", l.command}, {"args", l.args},
            {"auto_start", l.auto_start}
        });
    }
    j["lsp_servers"] = lsp;

    json mcp = json::array();
    for (auto& m : c.mcp_servers) {
        mcp.push_back({
            {"id", m.id}, {"name", m.name},
            {"command", m.command}, {"args", m.args},
            {"transport", m.transport}, {"url", m.url},
            {"auto_start", m.auto_start}
        });
    }
    j["mcp_servers"] = mcp;
}

void from_json(const json& j, Config& c) {
    auto get = [&](const std::string& key, auto& val) {
        if (j.contains(key)) j.at(key).get_to(val);
    };

    get("user_name", c.user_name);
    get("agent_name", c.agent_name);
    get("default_model", c.default_model);
    get("default_provider", c.default_provider);
    get("max_context_tokens", c.max_context_tokens);
    get("max_mpc_rounds", c.max_mpc_rounds);
    get("request_timeout_sec", c.request_timeout_sec);
    get("stream_output", c.stream_output);
    get("auto_memory", c.auto_memory);
    get("tui_enabled", c.tui_enabled);
    get("tui_theme", c.tui_theme);
    get("router_enabled", c.router_enabled);
    get("router_port", c.router_port);
    get("router_bind", c.router_bind);
    get("router_tls", c.router_tls);
    get("dangerous_commands", c.dangerous_commands);
    get("default_tool_permission", c.default_tool_permission);
    get("websearch_proxy", c.websearch_proxy);
    get("websearch_user_agent", c.websearch_user_agent);
    get("webfetch_proxy", c.webfetch_proxy);
    get("webfetch_max_size_mb", c.webfetch_max_size_mb);
    get("webfetch_timeout_sec", c.webfetch_timeout_sec);
    get("recent_projects", c.recent_projects);
    get("max_recent_projects", c.max_recent_projects);

    if (j.contains("themes")) {
        for (auto& [name, tj] : j["themes"].items()) {
            Config::ThemeConfig t;
            if (tj.contains("primary_color")) tj.at("primary_color").get_to(t.primary_color);
            if (tj.contains("secondary_color")) tj.at("secondary_color").get_to(t.secondary_color);
            if (tj.contains("success_color")) tj.at("success_color").get_to(t.success_color);
            if (tj.contains("warning_color")) tj.at("warning_color").get_to(t.warning_color);
            if (tj.contains("error_color")) tj.at("error_color").get_to(t.error_color);
            if (tj.contains("background_color")) tj.at("background_color").get_to(t.background_color);
            if (tj.contains("foreground_color")) tj.at("foreground_color").get_to(t.foreground_color);
            if (tj.contains("status_bar_color")) tj.at("status_bar_color").get_to(t.status_bar_color);
            if (tj.contains("input_color")) tj.at("input_color").get_to(t.input_color);
            if (tj.contains("border_color")) tj.at("border_color").get_to(t.border_color);
            c.themes[name] = t;
        }
    }

    if (j.contains("providers")) {
        for (auto& pj : j["providers"]) {
            Config::ProviderConfig p;
            pj.at("name").get_to(p.name);
            if (pj.contains("type")) pj.at("type").get_to(p.type);
            pj.at("api_base").get_to(p.api_base);
            if (pj.contains("api_key")) pj.at("api_key").get_to(p.api_key);
            pj.at("model").get_to(p.model);
            if (pj.contains("context_length")) pj.at("context_length").get_to(p.context_length);
            if (pj.contains("temperature")) pj.at("temperature").get_to(p.temperature);
            if (pj.contains("top_p")) pj.at("top_p").get_to(p.top_p);
            if (pj.contains("max_tokens")) pj.at("max_tokens").get_to(p.max_tokens);
            if (pj.contains("thinking_mode")) pj.at("thinking_mode").get_to(p.thinking_mode);
            if (pj.contains("thinking_budget")) pj.at("thinking_budget").get_to(p.thinking_budget);
            if (pj.contains("supports_vision")) pj.at("supports_vision").get_to(p.supports_vision);
            if (pj.contains("supports_tools")) pj.at("supports_tools").get_to(p.supports_tools);
            c.providers.push_back(p);
        }
    }

    if (j.contains("channels")) {
        for (auto& cj : j["channels"]) {
            Config::ChannelConfig ch;
            cj.at("type").get_to(ch.type);
            if (cj.contains("token")) cj.at("token").get_to(ch.token);
            if (cj.contains("enabled")) cj.at("enabled").get_to(ch.enabled);
            if (cj.contains("proxy")) cj.at("proxy").get_to(ch.proxy);
            c.channels.push_back(ch);
        }
    }

    if (j.contains("agents")) {
        for (auto& aj : j["agents"]) {
            Config::AgentConfig a;
            aj.at("name").get_to(a.name);
            if (aj.contains("system_prompt")) aj.at("system_prompt").get_to(a.system_prompt);
            if (aj.contains("allowed_tools")) aj.at("allowed_tools").get_to(a.allowed_tools);
            if (aj.contains("allow_all_tools")) aj.at("allow_all_tools").get_to(a.allow_all_tools);
            if (aj.contains("max_sub_agents")) aj.at("max_sub_agents").get_to(a.max_sub_agents);
            c.agents.push_back(a);
        }
    }

    if (j.contains("lsp_servers")) {
        for (auto& lj : j["lsp_servers"]) {
            Config::LSPServerEntry l;
            lj.at("id").get_to(l.id);
            if (lj.contains("language")) lj.at("language").get_to(l.language);
            if (lj.contains("command")) lj.at("command").get_to(l.command);
            if (lj.contains("args")) lj.at("args").get_to(l.args);
            if (lj.contains("auto_start")) lj.at("auto_start").get_to(l.auto_start);
            c.lsp_servers.push_back(l);
        }
    }

    if (j.contains("mcp_servers")) {
        for (auto& mj : j["mcp_servers"]) {
            Config::MCPServerEntry m;
            mj.at("id").get_to(m.id);
            if (mj.contains("name")) mj.at("name").get_to(m.name);
            if (mj.contains("command")) mj.at("command").get_to(m.command);
            if (mj.contains("args")) mj.at("args").get_to(m.args);
            if (mj.contains("transport")) mj.at("transport").get_to(m.transport);
            if (mj.contains("url")) mj.at("url").get_to(m.url);
            if (mj.contains("auto_start")) mj.at("auto_start").get_to(m.auto_start);
            c.mcp_servers.push_back(m);
        }
    }
}

} // namespace agent
