#pragma once
#include "core/module.hpp"
#include "components/service/service.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace agent {

struct MCPServerConfig {
    std::string id;
    std::string name;
    std::string command;
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    bool auto_start = false;
    std::string transport = "stdio";
    std::string url;
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

struct MCPPrompt {
    std::string name;
    std::string description;
    nlohmann::json arguments_schema;
};

class MCP : public Module<MCP> {
public:
    static constexpr std::string_view static_name() { return "mcp"; }

    void on_initialize();
    void on_shutdown();

    void add_server(const MCPServerConfig& config);
    void remove_server(std::string_view id);
    void start_server(std::string_view id);
    void stop_server(std::string_view id);
    std::vector<MCPServerConfig> list_servers() const;
    bool is_server_running(std::string_view id) const;

    std::vector<MCPToolInfo> list_tools(std::string_view server_id);
    std::vector<MCPToolInfo> list_all_tools();

    nlohmann::json call_tool(std::string_view server_id, std::string_view tool_name,
                              const nlohmann::json& arguments);

    std::vector<MCPResource> list_resources(std::string_view server_id);
    std::string read_resource(std::string_view server_id, std::string_view uri);

    std::vector<MCPPrompt> list_prompts(std::string_view server_id);
    nlohmann::json get_prompt(std::string_view server_id, std::string_view prompt_name,
                               const nlohmann::json& arguments);

private:
    struct ServerConnection {
        MCPServerConfig config;
        StdioProcess process;
        bool initialized = false;
        std::atomic<int> next_request_id{1};
        std::mutex io_mutex;
        std::vector<MCPToolInfo> cached_tools;
    };

    nlohmann::json send_request(ServerConnection& conn, std::string_view method,
                                  const nlohmann::json& params = {});
    void send_notification(ServerConnection& conn, std::string_view method,
                            const nlohmann::json& params = {});
    bool initialize_server(ServerConnection& conn);

    std::map<std::string, std::unique_ptr<ServerConnection>> m_connections;
    std::mutex m_mutex;
};

} // namespace agent
