#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace agent {

class Config {
public:
    static Config& instance();

    void load();
    void save() const;
    void reset();

    fs::path exe_dir() const;
    fs::path agent_dir() const;
    fs::path project_dir() const;
    void set_project_dir(const fs::path& dir);

    fs::path session_dir() const;
    fs::path memory_dir() const;
    fs::path edit_history_dir() const;
    fs::path assets_dir() const;
    fs::path keychain_path() const;

    // ── System ─────────────────────────────────────────────────
    std::string user_name = "User";
    std::string agent_name = "Agent";
    std::string default_model = "gpt-4o";
    std::string default_provider = "llama-server";
    int max_context_tokens = 128000;
    int max_mpc_rounds = 30;
    int request_timeout_sec = 120;
    bool stream_output = true;
    bool auto_memory = true;

    // ── TUI ────────────────────────────────────────────────────
    bool tui_enabled = true;
    std::string tui_theme = "default";

    // ── Router ─────────────────────────────────────────────────
    bool router_enabled = false;
    int router_port = 18080;
    std::string router_bind = "127.0.0.1";
    bool router_tls = false;
    std::string router_cert_path;
    std::string router_key_path;
    std::string router_password_hash;

    // ── Provider settings ──────────────────────────────────────
    struct ProviderConfig {
        std::string name;
        std::string type;       // "openai", "anthropic", "ollama", "llama_server"
        std::string api_base;
        std::string api_key;
        std::string model;
        int context_length = 128000;
        float temperature = 0.7f;
        float top_p = 1.0f;
        int max_tokens = 4096;
        std::string thinking_mode;
        int thinking_budget = 16000;
        bool supports_vision = false;
        bool supports_tools = true;
        std::map<std::string, std::string> extra_headers;
    };

    std::vector<ProviderConfig> providers;
    ProviderConfig* current_provider() {
        for (auto& p : providers) {
            if (p.name == default_provider) return &p;
        }
        return providers.empty() ? nullptr : &providers[0];
    }

    // ── Channel settings ───────────────────────────────────────
    struct ChannelConfig {
        std::string type;
        std::string token;
        std::string proxy;
        bool enabled = false;
    };
    std::vector<ChannelConfig> channels;

    // ── Permission settings ────────────────────────────────────
    std::vector<std::string> auto_allowed_tools;
    std::vector<std::string> dangerous_commands = {
        "rm -rf /", "rm -rf /*", "rmdir /s", "del /f /q",
        "shutdown", "reboot", "halt", "mkfs", "dd if=",
        "fdisk", "format", ":(){ :|:& };:",
        "chmod 777", "sudo", "su -", "passwd",
        "git push --force", "git push -f",
        "git reset --hard", "git clean -fdx"
    };
    std::string default_tool_permission = "ask";

    // ── Agent settings ─────────────────────────────────────────
    struct AgentConfig {
        std::string name;
        std::string system_prompt;
        std::vector<std::string> allowed_tools;
        bool allow_all_tools = true;
        int max_sub_agents = 5;
    };
    std::vector<AgentConfig> agents;

    // ── WebSearch settings ─────────────────────────────────────
    std::string websearch_proxy;
    std::string websearch_user_agent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36";
    std::string bing_api_key;
    std::string google_api_key;
    std::string google_cse_id;

    // ── WebFetch settings ──────────────────────────────────────
    std::string webfetch_proxy;
    int webfetch_max_size_mb = 50;
    int webfetch_timeout_sec = 60;

    // ── LSP settings ───────────────────────────────────────────
    struct LSPServerEntry {
        std::string id;
        std::string language;
        std::string command;
        std::vector<std::string> args;
        bool auto_start = false;
    };
    std::vector<LSPServerEntry> lsp_servers;

    // ── MCP settings ───────────────────────────────────────────
    struct MCPServerEntry {
        std::string id;
        std::string name;
        std::string command;
        std::vector<std::string> args;
        std::string transport = "stdio";
        std::string url;
        bool auto_start = false;
    };
    std::vector<MCPServerEntry> mcp_servers;

    // ── Multi-project support ──────────────────────────────────
    std::vector<std::string> recent_projects;
    int max_recent_projects = 20;

    friend void to_json(nlohmann::json& j, const Config& c);
    friend void from_json(const nlohmann::json& j, Config& c);

private:
    Config() = default;
    fs::path config_path() const;
    fs::path m_project_dir;
    mutable std::mutex m_mutex;
};

void to_json(nlohmann::json& j, const Config& c);
void from_json(const nlohmann::json& j, Config& c);

} // namespace agent
