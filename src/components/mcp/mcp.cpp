#include "components/mcp/mcp.hpp"
#include "components/tools/tools.hpp"
#include "core/logger.hpp"
#include <sstream>
#include <iostream>

namespace agent {

using json = nlohmann::json;

void MCP::on_initialize() {
    auto& cfg = Config::instance();
    for (auto& entry : cfg.mcp_servers) {
        MCPServerConfig sc;
        sc.id = entry.id;
        sc.name = entry.name;
        sc.command = entry.command;
        sc.args = entry.args;
        sc.transport = entry.transport;
        sc.url = entry.url;
        sc.auto_start = entry.auto_start;
        add_server(sc);
    }
}

void MCP::on_shutdown() {
    std::lock_guard lock(m_mutex);
    for (auto& [id, conn] : m_connections) {
        if (conn->process.is_running()) {
            conn->process.terminate();
        }
    }
    m_connections.clear();
}

void MCP::add_server(const MCPServerConfig& config) {
    std::lock_guard lock(m_mutex);
    auto conn = std::make_unique<ServerConnection>();
    conn->config = config;
    auto* ptr = conn.get();
    m_connections[config.id] = std::move(conn);

    if (config.auto_start) {
        start_server(config.id);
    }
}

void MCP::remove_server(std::string_view id) {
    stop_server(id);
    std::lock_guard lock(m_mutex);
    m_connections.erase(std::string(id));
}

void MCP::start_server(std::string_view id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(id));
    if (it == m_connections.end()) return;

    auto& conn = *it->second;
    if (conn.process.is_running()) return;

    if (!conn.process.spawn(conn.config.command, conn.config.args, ".", conn.config.env)) {
        LOG_ERROR("MCP", "Failed to spawn server: " + std::string(id));
        return;
    }

    if (!initialize_server(conn)) {
        LOG_ERROR("MCP", "Failed to initialize server: " + std::string(id));
        conn.process.terminate();
        return;
    }

    auto tools = list_tools(id);
    if (!tools.empty()) {
        auto& tool_registry = ModuleRegistry::instance().require<Tools>();
        std::vector<ToolInfo> tool_infos;
        for (auto& t : tools) {
            ToolInfo ti;
            ti.name = t.tool_name;
            ti.description = "[MCP:" + std::string(id) + "] " + t.description;
            ti.categories = {"mcp", std::string(id)};
            ti.requires_permission = true;
            tool_infos.push_back(ti);
        }
        tool_registry.add_mcp_tools(id, tool_infos);
    }
}

void MCP::stop_server(std::string_view id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(id));
    if (it == m_connections.end()) return;

    auto& conn = *it->second;
    if (conn.process.is_running()) {
        send_notification(conn, "notifications/exit");
        conn.process.close_stdin();
        conn.process.wait(2000);
        if (conn.process.is_running()) {
            conn.process.terminate();
        }
    }

    auto* tools = ModuleRegistry::instance().get<Tools>();
    if (tools) tools->remove_mcp_tools(id);

    conn.initialized = false;
}

std::vector<MCPServerConfig> MCP::list_servers() const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    std::vector<MCPServerConfig> servers;
    for (auto& [id, conn] : m_connections) {
        servers.push_back(conn->config);
    }
    return servers;
}

bool MCP::is_server_running(std::string_view id) const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    auto it = m_connections.find(std::string(id));
    if (it == m_connections.end()) return false;
    return it->second->process.is_running();
}

bool MCP::initialize_server(ServerConnection& conn) {
    json init_params = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", {{"listChanged", true}}},
            {"resources", {{"subscribe", false}, {"listChanged", true}}},
            {"prompts", {{"listChanged", true}}}
        }},
        {"clientInfo", {
            {"name", "agent.cpp"},
            {"version", AGENT_VERSION}
        }}
    };

    auto response = send_request(conn, "initialize", init_params);
    if (response.is_null() || response.contains("error")) {
        return false;
    }

    send_notification(conn, "notifications/initialized");
    conn.initialized = true;
    return true;
}

json MCP::send_request(ServerConnection& conn, std::string_view method,
                         const json& params) {
    std::lock_guard lock(conn.io_mutex);

    int id = conn.next_request_id++;

    json request = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        request["params"] = params;
    }

    std::string msg = request.dump() + "\n";
    if (!conn.process.write(msg)) {
        return {{"error", {{"code", -32000}, {"message", "Failed to write to server"}}}};
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string line = conn.process.read_line(5000);
        if (line.empty()) {
            if (!conn.process.is_running()) {
                return {{"error", {{"code", -32000}, {"message", "Server process died"}}}};
            }
            continue;
        }

        try {
            json resp = json::parse(line);

            if (resp.contains("method") && !resp.contains("id")) {
                continue;
            }

            if (resp.contains("id") && resp["id"].get<int>() == id) {
                return resp;
            }
        } catch (const json::parse_error&) {
            continue;
        }
    }

    return {{"error", {{"code", -32000}, {"message", "Request timeout"}}}};
}

void MCP::send_notification(ServerConnection& conn, std::string_view method,
                              const json& params) {
    std::lock_guard lock(conn.io_mutex);

    json notification = {
        {"jsonrpc", "2.0"},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        notification["params"] = params;
    }

    std::string msg = notification.dump() + "\n";
    conn.process.write(msg);
}

std::vector<MCPToolInfo> MCP::list_tools(std::string_view server_id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(server_id));
    if (it == m_connections.end()) return {};

    auto& conn = *it->second;
    if (!conn.initialized || !conn.process.is_running()) return {};

    auto response = send_request(conn, "tools/list");
    if (response.contains("error")) return {};

    std::vector<MCPToolInfo> tools;
    if (response.contains("result") && response["result"].contains("tools")) {
        for (auto& t : response["result"]["tools"]) {
            MCPToolInfo info;
            info.server_id = std::string(server_id);
            info.tool_name = t.value("name", "");
            info.description = t.value("description", "");
            info.input_schema = t.value("inputSchema", json::object());
            tools.push_back(info);
        }
    }

    conn.cached_tools = tools;
    return tools;
}

std::vector<MCPToolInfo> MCP::list_all_tools() {
    std::vector<MCPToolInfo> all;
    std::lock_guard lock(m_mutex);
    for (auto& [id, conn] : m_connections) {
        if (conn->initialized && conn->process.is_running()) {
            for (auto& t : conn->cached_tools) {
                all.push_back(t);
            }
        }
    }
    return all;
}

json MCP::call_tool(std::string_view server_id, std::string_view tool_name,
                      const json& arguments) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(server_id));
    if (it == m_connections.end()) {
        return {{"error", "Server not found: " + std::string(server_id)}};
    }

    auto& conn = *it->second;
    if (!conn.initialized || !conn.process.is_running()) {
        return {{"error", "Server not running"}};
    }

    json params = {
        {"name", tool_name},
        {"arguments", arguments}
    };

    auto response = send_request(conn, "tools/call", params);
    if (response.contains("error")) {
        return response["error"];
    }

    return response.value("result", json::object());
}

std::vector<MCPResource> MCP::list_resources(std::string_view server_id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(server_id));
    if (it == m_connections.end()) return {};

    auto& conn = *it->second;
    if (!conn.initialized) return {};

    auto response = send_request(conn, "resources/list");
    if (response.contains("error")) return {};

    std::vector<MCPResource> resources;
    if (response.contains("result") && response["result"].contains("resources")) {
        for (auto& r : response["result"]["resources"]) {
            MCPResource res;
            res.uri = r.value("uri", "");
            res.name = r.value("name", "");
            res.description = r.value("description", "");
            res.mime_type = r.value("mimeType", "");
            resources.push_back(res);
        }
    }
    return resources;
}

std::string MCP::read_resource(std::string_view server_id, std::string_view uri) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(server_id));
    if (it == m_connections.end()) return "";

    auto& conn = *it->second;
    if (!conn.initialized) return "";

    json params = {{"uri", uri}};
    auto response = send_request(conn, "resources/read", params);
    if (response.contains("error")) return "";

    if (response.contains("result") && response["result"].contains("contents")) {
        auto& contents = response["result"]["contents"];
        if (!contents.empty() && contents[0].contains("text")) {
            return contents[0]["text"].get<std::string>();
        }
    }
    return "";
}

std::vector<MCPPrompt> MCP::list_prompts(std::string_view server_id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(server_id));
    if (it == m_connections.end()) return {};

    auto& conn = *it->second;
    if (!conn.initialized) return {};

    auto response = send_request(conn, "prompts/list");
    if (response.contains("error")) return {};

    std::vector<MCPPrompt> prompts;
    if (response.contains("result") && response["result"].contains("prompts")) {
        for (auto& p : response["result"]["prompts"]) {
            MCPPrompt prompt;
            prompt.name = p.value("name", "");
            prompt.description = p.value("description", "");
            prompt.arguments_schema = p.value("arguments", json::array());
            prompts.push_back(prompt);
        }
    }
    return prompts;
}

json MCP::get_prompt(std::string_view server_id, std::string_view prompt_name,
                       const json& arguments) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(server_id));
    if (it == m_connections.end()) return {{"error", "Server not found"}};

    auto& conn = *it->second;
    if (!conn.initialized) return {{"error", "Server not initialized"}};

    json params = {
        {"name", prompt_name},
        {"arguments", arguments}
    };

    auto response = send_request(conn, "prompts/get", params);
    if (response.contains("error")) return response["error"];
    return response.value("result", json::object());
}

} // namespace agent
