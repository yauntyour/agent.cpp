#pragma once
#include "core/module.hpp"
#include "components/provider/provider.hpp"
#include "components/tools/tools.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <future>
#include <nlohmann/json.hpp>

namespace agent {

enum class AgentType {
    MainCoder,
    SubAgent,
    Explorer
};

enum class SubAgentMode {
    Background,     // Silent completion, notify main agent when done
    Foreground,     // Wait for completion
    Interactive     // Allow tool interaction
};

struct AgentConfig {
    AgentType type;
    std::string name;
    std::string system_prompt;
    std::vector<std::string> allowed_tools;
    bool allow_all_tools = true;
    int max_iterations = 30;
    int max_context_tokens = 128000;
    bool inherit_memory = true;
    SubAgentMode mode = SubAgentMode::Foreground;
};

struct AgentResult {
    bool success;
    std::string output;
    std::string error;
    nlohmann::json tool_calls_log;
    nlohmann::json usage;
    int iterations_used;
};

class Agent : public Module<Agent> {
public:
    static constexpr std::string_view static_name() { return "agent"; }

    void on_initialize();
    void on_shutdown();

    // ── Agent configuration ────────────────────────────────────
    void set_system_prompt(AgentType type, std::string_view prompt);
    std::string get_system_prompt(AgentType type) const;
    void set_tool_restrictions(AgentType type, const std::vector<std::string>& allowed_tools);

    // ── Main agent execution ───────────────────────────────────
    using ProgressCallback = std::function<void(std::string_view thought, std::string_view action)>;

    AgentResult execute(std::string_view user_input,
                        ProgressCallback progress = nullptr,
                        bool stream = true);

    // ── Sub-agent execution ────────────────────────────────────
    AgentResult execute_sub_agent(const AgentConfig& config,
                                  std::string_view task,
                                  ProgressCallback progress = nullptr);

    AgentResult execute_explorer(std::string_view task,
                                 ProgressCallback progress = nullptr);

    // ── MPC loop (Model-Protocol-Command) ──────────────────────
    struct MPCState {
        std::vector<ChatMessage> messages;
        int iteration;
        int total_tokens;
        bool finished;
    };

    MPCState get_mpc_state() const;
    void reset_mpc_state();

    // ── Background sub-agents ──────────────────────────────────
    std::string launch_background_sub_agent(const AgentConfig& config, std::string_view task);
    std::future<AgentResult> get_background_result(std::string_view agent_id);
    void cancel_background_agent(std::string_view agent_id);

private:
    AgentResult run_mpc_loop(const AgentConfig& config,
                             const std::vector<ChatMessage>& initial_messages,
                             ProgressCallback progress,
                             bool stream);

    ChatMessage parse_tool_call(std::string_view response);
    std::string execute_tool_calls(const std::vector<ToolCall>& calls);

    std::map<AgentType, std::string> m_system_prompts;
    std::map<AgentType, std::vector<std::string>> m_tool_restrictions;
    MPCState m_state;

};

} // namespace agent
