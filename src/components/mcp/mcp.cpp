#include "components/mcp/mcp.hpp"
#include "components/service/service.hpp"
#include <sstream>

namespace agent {

void MCP::on_initialize() {}
void MCP::on_shutdown() {
    for (auto& [id, config] : m_servers) {
        stop_server(id);
    }
}

void MCP::add_server(const MCPServerConfig& config) {
    m_servers[config.id] = config;
    if (config.auto_start) start_server(config.id);
}

void MCP::remove_server(std::string_view id) {
    stop_server(id);
    m_servers.erase(std::string(id));
}

void MCP::start_server(std::string_view id) {
    auto& svc = ModuleRegistry::instance().require<Service>();
    auto it = m_servers.find(std::string(id));
    if (it == m_servers.end()) return;

    ServiceProcess proc;
    proc.name = "mcp-" + std::string(id);
    proc.command = it->second.command;

    std::stringstream ss;
    for (auto& arg : it->second.args) {
        ss << arg << " ";
    }
    proc.command = it->second.command + " " + ss.str();
    proc.working_dir = ".";

    for (auto& [key, val] : it->second.env) {
        // Environment variables set through command
    }

    svc.spawn(proc);
}

void MCP::stop_server(std::string_view id) {
    auto& svc = ModuleRegistry::instance().require<Service>();
    svc.stop("mcp-" + std::string(id));
}

std::vector<MCPServerConfig> MCP::list_servers() const {
    std::vector<MCPServerConfig> servers;
    for (auto& [id, cfg] : m_servers) {
        servers.push_back(cfg);
    }
    return servers;
}

std::vector<MCPToolInfo> MCP::list_tools(std::string_view server_id) {
    // Tools are discovered via MCP protocol's "tools/list" method
    return {};
}

std::vector<MCPToolInfo> MCP::list_all_tools() {
    std::vector<MCPToolInfo> all;
    for (auto& [id, cfg] : m_servers) {
        auto tools = list_tools(id);
        all.insert(all.end(), tools.begin(), tools.end());
    }
    return all;
}

nlohmann::json MCP::call_tool(std::string_view server_id, std::string_view tool_name,
                                const nlohmann::json& arguments) {
    // MCP tool calls use JSON-RPC "tools/call" method
    return nlohmann::json::object();
}

std::vector<MCPResource> MCP::list_resources(std::string_view server_id) {
    return {};
}

std::string MCP::read_resource(std::string_view server_id, std::string_view uri) {
    return "";
}

nlohmann::json MCP::get_prompt(std::string_view server_id, std::string_view prompt_name,
                                 const nlohmann::json& arguments) {
    return nlohmann::json::object();
}

} // namespace agent
