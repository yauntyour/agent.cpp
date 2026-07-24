#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

struct MCPServerConfig {
    std::string id;
    std::string name;
    std::string command;        // server command to spawn
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool auto_start = false;
    std::string transport;      // "stdio", "sse"
    std::string url;            // for SSE transport
};

struct MCPToolInfo {
    std::string server_id;
    std::string tool_name;
    std::string description;
    nlohmann::json input_schema;
};

struct MCPResource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

class MCP : public Module<MCP> {
public:
    static constexpr std::string_view static_name() { return "mcp"; }

    void on_initialize();
    void on_shutdown();

    // ── Server management ──────────────────────────────────────
    void add_server(const MCPServerConfig& config);
    void remove_server(std::string_view id);
    void start_server(std::string_view id);
    void stop_server(std::string_view id);
    std::vector<MCPServerConfig> list_servers() const;

    // ── Tool discovery ─────────────────────────────────────────
    std::vector<MCPToolInfo> list_tools(std::string_view server_id);
    std::vector<MCPToolInfo> list_all_tools();

    // ── Tool execution ─────────────────────────────────────────
    nlohmann::json call_tool(std::string_view server_id, std::string_view tool_name,
                             const nlohmann::json& arguments);

    // ── Resource access ────────────────────────────────────────
    std::vector<MCPResource> list_resources(std::string_view server_id);
    std::string read_resource(std::string_view server_id, std::string_view uri);

    // ── Prompts ────────────────────────────────────────────────
    nlohmann::json get_prompt(std::string_view server_id, std::string_view prompt_name,
                              const nlohmann::json& arguments);

private:
    std::map<std::string, MCPServerConfig> m_servers;

};

} // namespace agent
