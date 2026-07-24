#pragma once
#include "core/module.hpp"
#include "core/config.hpp"
#include "components/provider/provider.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

struct ToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

struct ToolResult {
    std::string call_id;
    std::string content;
    bool is_error = false;
    std::string error_message;
};

struct ToolInfo {
    std::string name;
    std::string description;
    std::vector<std::string> categories;
    bool requires_permission = true;
    bool can_run_async = false;
};

using ToolExecutor = std::function<ToolResult(const ToolCall&)>;

class Tools : public Module<Tools> {
public:
    static constexpr std::string_view static_name() { return "tools"; }

    void on_initialize();
    void on_shutdown();

    // ── Tool registration ──────────────────────────────────────
    void register_tool(const ToolInfo& info, ToolExecutor executor);
    void unregister_tool(std::string_view name);
    bool has_tool(std::string_view name) const;
    std::vector<ToolInfo> list_tools() const;
    std::vector<ToolDefinition> get_definitions() const;

    // ── Tool execution ─────────────────────────────────────────
    ToolResult execute(const ToolCall& call);
    std::vector<ToolResult> execute_batch(const std::vector<ToolCall>& calls);
    ToolResult execute_async(const ToolCall& call);

    // ── Built-in tools ─────────────────────────────────────────
    ToolResult tool_read(const ToolCall& call);
    ToolResult tool_write(const ToolCall& call);
    ToolResult tool_edit(const ToolCall& call);
    ToolResult tool_search(const ToolCall& call);
    ToolResult tool_exec(const ToolCall& call);
    ToolResult tool_task(const ToolCall& call);
    ToolResult tool_question(const ToolCall& call);
    ToolResult tool_websearch(const ToolCall& call);
    ToolResult tool_webfetch(const ToolCall& call);
    ToolResult tool_mind_map(const ToolCall& call);
    ToolResult tool_todolist(const ToolCall& call);
    ToolResult tool_memory(const ToolCall& call);
    ToolResult tool_image(const ToolCall& call);
    ToolResult tool_fs(const ToolCall& call);
    ToolResult tool_subagent(const ToolCall& call);
    ToolResult tool_git_saved(const ToolCall& call);
    ToolResult tool_git_restore(const ToolCall& call);

    // ── MCP integration ────────────────────────────────────────
    void add_mcp_tools(std::string_view server_id, const std::vector<ToolInfo>& tools);
    void remove_mcp_tools(std::string_view server_id);

private:
    void register_builtin_tools();
    void register_mcp_placeholder();

    std::map<std::string, ToolInfo> m_tool_info;
    std::map<std::string, ToolExecutor> m_executors;
    std::map<std::string, std::vector<std::string>> m_mcp_tools;

    nlohmann::json m_todos = nlohmann::json::array();

};

} // namespace agent
