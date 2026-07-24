#include "core/config.hpp"
#include "utils/crypto.hpp"
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

fs::path Config::cache_dir() const {
    auto dir = exe_dir() / ".cache";
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    return dir;
}

fs::path Config::workspace_dir() const {
    return cache_dir() / "workspace";
}

fs::path Config::config_path() const {
    return cache_dir() / "config.json";
}

fs::path Config::session_dir() const {
    return cache_dir() / "sessions";
}

fs::path Config::memory_dir() const {
    return cache_dir() / "memorys";
}

fs::path Config::tools_dir() const {
    return cache_dir() / "tools";
}

void Config::load() {
    auto path = config_path();
    if (!fs::exists(path)) {
        save();
        return;
    }
    std::ifstream f(path);
    json j;
    f >> j;
    j.get_to(*this);
}

void Config::save() const {
    auto dir = cache_dir();
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }
    json j;
    to_json(j, *this);
    std::ofstream f(config_path());
    f << j.dump(2);
}

void Config::reset() {
    *this = Config{};
    auto dir = cache_dir();
    if (fs::exists(dir)) {
        fs::remove_all(dir);
    }
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
        {"webfetch_proxy", c.webfetch_proxy},
        {"webfetch_max_size_mb", c.webfetch_max_size_mb},
        {"recent_projects", c.recent_projects},
    };

    json providers = json::array();
    for (auto& p : c.providers) {
        json pj;
        pj["name"] = p.name;
        pj["api_base"] = p.api_base;
        pj["api_key"] = p.api_key;  // Note: encrypted in keychain
        pj["model"] = p.model;
        pj["context_length"] = p.context_length;
        pj["temperature"] = p.temperature;
        pj["top_p"] = p.top_p;
        pj["max_tokens"] = p.max_tokens;
        pj["thinking_mode"] = p.thinking_mode;
        pj["thinking_budget"] = p.thinking_budget;
        providers.push_back(pj);
    }
    j["providers"] = providers;

    json channels = json::array();
    for (auto& ch : c.channels) {
        json cj;
        cj["type"] = ch.type;
        cj["enabled"] = ch.enabled;
        cj["proxy"] = ch.proxy;
        channels.push_back(cj);
    }
    j["channels"] = channels;

    json agents = json::array();
    for (auto& a : c.agents) {
        json aj;
        aj["name"] = a.name;
        aj["system_prompt"] = a.system_prompt;
        aj["allowed_tools"] = a.allowed_tools;
        aj["allow_all_tools"] = a.allow_all_tools;
        aj["max_sub_agents"] = a.max_sub_agents;
        agents.push_back(aj);
    }
    j["agents"] = agents;
}

void from_json(const json& j, Config& c) {
    j.at("user_name").get_to(c.user_name);
    j.at("agent_name").get_to(c.agent_name);
    j.at("default_model").get_to(c.default_model);
    j.at("default_provider").get_to(c.default_provider);
    j.at("max_context_tokens").get_to(c.max_context_tokens);
    j.at("max_mpc_rounds").get_to(c.max_mpc_rounds);
    if (j.contains("request_timeout_sec")) j.at("request_timeout_sec").get_to(c.request_timeout_sec);
    if (j.contains("stream_output")) j.at("stream_output").get_to(c.stream_output);
    if (j.contains("auto_memory")) j.at("auto_memory").get_to(c.auto_memory);
    if (j.contains("tui_enabled")) j.at("tui_enabled").get_to(c.tui_enabled);
    if (j.contains("tui_theme")) j.at("tui_theme").get_to(c.tui_theme);
    if (j.contains("router_enabled")) j.at("router_enabled").get_to(c.router_enabled);
    if (j.contains("router_port")) j.at("router_port").get_to(c.router_port);
    if (j.contains("router_bind")) j.at("router_bind").get_to(c.router_bind);
    if (j.contains("router_tls")) j.at("router_tls").get_to(c.router_tls);
    if (j.contains("dangerous_commands")) j.at("dangerous_commands").get_to(c.dangerous_commands);
    if (j.contains("default_tool_permission")) j.at("default_tool_permission").get_to(c.default_tool_permission);
    if (j.contains("websearch_proxy")) j.at("websearch_proxy").get_to(c.websearch_proxy);
    if (j.contains("webfetch_proxy")) j.at("webfetch_proxy").get_to(c.webfetch_proxy);
    if (j.contains("webfetch_max_size_mb")) j.at("webfetch_max_size_mb").get_to(c.webfetch_max_size_mb);
    if (j.contains("recent_projects")) j.at("recent_projects").get_to(c.recent_projects);

    if (j.contains("providers")) {
        for (auto& pj : j["providers"]) {
            Config::ProviderConfig p;
            pj.at("name").get_to(p.name);
            pj.at("api_base").get_to(p.api_base);
            if (pj.contains("api_key")) pj.at("api_key").get_to(p.api_key);
            pj.at("model").get_to(p.model);
            if (pj.contains("context_length")) pj.at("context_length").get_to(p.context_length);
            if (pj.contains("temperature")) pj.at("temperature").get_to(p.temperature);
            if (pj.contains("top_p")) pj.at("top_p").get_to(p.top_p);
            if (pj.contains("max_tokens")) pj.at("max_tokens").get_to(p.max_tokens);
            if (pj.contains("thinking_mode")) pj.at("thinking_mode").get_to(p.thinking_mode);
            if (pj.contains("thinking_budget")) pj.at("thinking_budget").get_to(p.thinking_budget);
            c.providers.push_back(p);
        }
    }

    if (j.contains("channels")) {
        for (auto& cj : j["channels"]) {
            Config::ChannelConfig ch;
            cj.at("type").get_to(ch.type);
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
}

} // namespace agent
