#include "components/agent/agent.hpp"
#include "components/tools/tools.hpp"
#include "components/session/session.hpp"
#include "components/notice/notice.hpp"
#include "components/memory/memory.hpp"
#include "components/permission/permission.hpp"
#include <sstream>
#include <regex>

namespace agent {

using json = nlohmann::json;

void Agent::on_initialize() {
    // Default system prompts
    m_system_prompts[AgentType::MainCoder] = R"(
You are an AI coding agent. You have access to tools to read, write,
edit, search, and execute code. When given a task, break it down into
steps and use the appropriate tools to complete each step.

Important rules:
- Read files before editing them
- Use edit tool for precise changes
- Execute commands to build/test when needed
- Search for relevant code before making changes
- Report what you did at each step
)";

    m_system_prompts[AgentType::SubAgent] = R"(
You are a sub-agent of the main coding agent. Complete the assigned task
using the available tools and return a concise summary of your findings.
)";

    m_system_prompts[AgentType::Explorer] = R"(
You are a codebase explorer. Your job is to understand and map out code
structures. Use only read, search, and fs tools. Report findings clearly
with file paths and line numbers.
)";

    // Default tool restrictions for explorer
    m_tool_restrictions[AgentType::Explorer] = {"read", "search", "fs", "image"};
    m_tool_restrictions[AgentType::SubAgent] = {};
    m_tool_restrictions[AgentType::MainCoder] = {};
}

void Agent::on_shutdown() {
    reset_mpc_state();
}

void Agent::set_system_prompt(AgentType type, std::string_view prompt) {
    m_system_prompts[type] = prompt;
}

std::string Agent::get_system_prompt(AgentType type) const {
    auto it = m_system_prompts.find(type);
    if (it != m_system_prompts.end()) return it->second;
    return "";
}

void Agent::set_tool_restrictions(AgentType type, const std::vector<std::string>& allowed_tools) {
    m_tool_restrictions[type] = allowed_tools;
}

// ── Main agent execution ──────────────────────────────────────────
AgentResult Agent::execute(std::string_view user_input,
                            ProgressCallback progress,
                            bool stream) {
    AgentConfig config;
    config.type = AgentType::MainCoder;
    config.system_prompt = m_system_prompts[AgentType::MainCoder];
    config.allow_all_tools = true;
    config.max_iterations = Config::instance().max_mpc_rounds;

    std::vector<ChatMessage> initial_messages;

    // Add system prompt
    ChatMessage sys_msg;
    sys_msg.role = "system";
    sys_msg.content = config.system_prompt;
    initial_messages.push_back(sys_msg);

    // Add tools description
    auto& tools = ModuleRegistry::instance().require<Tools>();
    auto tool_defs = tools.get_definitions();
    if (!tool_defs.empty()) {
        ChatMessage tool_info;
        tool_info.role = "system";
        tool_info.content = "Available tools:\n";
        for (auto& td : tool_defs) {
            tool_info.content += "- " + td.name + ": " + td.description + "\n";
        }
        initial_messages.push_back(tool_info);
    }

    // Add user message
    ChatMessage user_msg;
    user_msg.role = "user";
    user_msg.content = user_input;
    initial_messages.push_back(user_msg);

    return run_mpc_loop(config, initial_messages, progress, stream);
}

AgentResult Agent::execute_sub_agent(const AgentConfig& sub_config,
                                      std::string_view task,
                                      ProgressCallback progress) {
    AgentConfig config = sub_config;
    if (config.system_prompt.empty()) {
        config.system_prompt = m_system_prompts[AgentType::SubAgent];
    }
    config.type = AgentType::SubAgent;

    std::vector<ChatMessage> messages;

    ChatMessage sys_msg;
    sys_msg.role = "system";
    sys_msg.content = config.system_prompt;
    messages.push_back(sys_msg);

    ChatMessage task_msg;
    task_msg.role = "user";
    task_msg.content = task;
    messages.push_back(task_msg);

    return run_mpc_loop(config, messages, progress, true);
}

AgentResult Agent::execute_explorer(std::string_view task,
                                     ProgressCallback progress) {
    AgentConfig config;
    config.type = AgentType::Explorer;
    config.system_prompt = m_system_prompts[AgentType::Explorer];
    config.allowed_tools = m_tool_restrictions[AgentType::Explorer];
    config.allow_all_tools = false;
    config.max_iterations = 10;

    std::vector<ChatMessage> messages;

    ChatMessage sys_msg;
    sys_msg.role = "system";
    sys_msg.content = config.system_prompt;
    messages.push_back(sys_msg);

    ChatMessage task_msg;
    task_msg.role = "user";
    task_msg.content = task;
    messages.push_back(task_msg);

    return run_mpc_loop(config, messages, progress, true);
}

// ── MPC Loop ──────────────────────────────────────────────────────
AgentResult Agent::run_mpc_loop(const AgentConfig& config,
                                 const std::vector<ChatMessage>& initial_messages,
                                 ProgressCallback progress,
                                 bool stream) {
    AgentResult result;
    result.success = true;
    result.iterations_used = 0;

    auto& provider = ModuleRegistry::instance().require<Provider>();
    auto& tools = ModuleRegistry::instance().require<Tools>();
    auto& session = ModuleRegistry::instance().require<SessionManager>();
    auto& notice = ModuleRegistry::instance().require<Notice>();

    std::vector<ChatMessage> messages = initial_messages;

    // Add memory context if enabled
    if (config.inherit_memory) {
        auto& mem = ModuleRegistry::instance().require<Memory>();
        // Find relevant memories for the task
        std::string task_text;
        for (auto& msg : initial_messages) {
            if (msg.role == "user") task_text += msg.content + " ";
        }
        auto relevant = mem.search_text(task_text, 3);
        if (!relevant.empty()) {
            ChatMessage mem_msg;
            mem_msg.role = "system";
            mem_msg.content = "Relevant memories:\n";
            for (auto& r : relevant) {
                mem_msg.content += "- " + r.title + ": " + r.content + "\n";
            }
            messages.insert(messages.begin() + 1, mem_msg);
        }
    }

    for (int iteration = 0; iteration < config.max_iterations; ++iteration) {
        result.iterations_used = iteration + 1;

        // Check context usage
        auto percent = session.context_usage_percent(session.current_session().id);
        if (percent > 95.0) {
            notice.context_threshold(percent);
            notice.info("Context Warning", "Context usage at " +
                        std::to_string(static_cast<int>(percent)) +
                        "%. Consider starting a new session.");
        }

        // Get available tools for this agent type
        std::vector<ToolDefinition> available_tools;
        auto all_defs = tools.get_definitions();
        if (config.allow_all_tools) {
            available_tools = all_defs;
        } else {
            for (auto& td : all_defs) {
                if (std::find(config.allowed_tools.begin(), config.allowed_tools.end(), td.name)
                    != config.allowed_tools.end()) {
                    available_tools.push_back(td);
                }
            }
        }

        std::string response;
        try {
            if (stream) {
                std::string full_response;
                provider.generate_stream(messages,
                    [&](const StreamingChunk& chunk) {
                        if (!chunk.content.empty()) {
                            full_response += chunk.content;
                            if (progress) progress("thinking", chunk.content);
                        }
                        if (chunk.finished) {
                            if (progress) progress("done", "");
                        }
                    },
                    available_tools);

                response = full_response;
            } else {
                response = provider.generate(messages, available_tools);
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.error = e.what();
            return result;
        }

        // Add assistant response to messages
        ChatMessage assistant_msg;
        assistant_msg.role = "assistant";
        assistant_msg.content = response;
        messages.push_back(assistant_msg);

        // Save to session
        SessionMessage sm;
        sm.role = "assistant";
        sm.content = response;
        sm.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        session.add_message(sm);

        // Parse tool calls from response
        auto tool_msg = parse_tool_call(response);
        if (tool_msg.role.empty()) {
            // No tool call, agent is done
            result.output = response;
            break;
        }

        // Execute tool calls
        std::vector<ToolCall> calls;
        if (!tool_msg.tool_calls.empty()) {
            for (auto& tc : tool_msg.tool_calls) {
                ToolCall call;
                call.id = tc.value("id", "tool_" + std::to_string(iteration));
                call.name = tc["function"].value("name", "");
                call.arguments = tc["function"].value("arguments", json{});
                calls.push_back(call);
            }
        }

        // Check permissions
        auto& permission = ModuleRegistry::instance().require<Permission>();
        for (auto& call : calls) {
            if (permission.should_ask(call.name)) {
                notice.permission_request(call.name, "Agent wants to use tool: " + call.name);
            }
        }

        // Execute tools
        auto results = tools.execute_batch(calls);

        // Add tool results to messages
        for (auto& tr : results) {
            ChatMessage tool_result_msg;
            tool_result_msg.role = "tool";
            tool_result_msg.content = tr.content;
            tool_result_msg.tool_call_id = tr.call_id;
            if (tr.is_error) {
                tool_result_msg.content = "Error: " + tr.error_message + "\n" + tr.content;
            }
            messages.push_back(tool_result_msg);

            // Save to session
            SessionMessage tsm;
            tsm.role = "tool";
            tsm.content = tr.content;
            tsm.tool_call_id = tr.call_id;
            tsm.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            session.add_message(tsm);
        }

        if (progress) progress("action", "Executed " + std::to_string(calls.size()) + " tool(s)");

        // Log tool calls
        result.tool_calls_log.push_back({
            {"iteration", iteration},
            {"calls", calls.size()}
        });
    }

    return result;
}

// ── Tool call parsing ─────────────────────────────────────────────
ChatMessage Agent::parse_tool_call(std::string_view response) {
    ChatMessage msg;
    std::string resp(response);

    // Try to parse XML-style tool calls: <tool>name:params</tool>
    std::regex xml_regex("<tool\\s+name=\"([^\"]+)\"\\s*>([\\s\\S]*?)</tool>",
                          std::regex::ECMAScript);
    std::regex xml_attr_regex("<tool\\s+(\\w+)=\"([^\"]+)\"(?:\\s+(\\w+)=\"([^\"]+)\")*\\s*/>",
                               std::regex::ECMAScript);

    std::smatch match;
    if (std::regex_search(resp, match, xml_regex) ||
        std::regex_search(resp, match, xml_attr_regex)) {
        msg.tool_calls = json::array();

        std::string::const_iterator start = resp.begin();
        std::string::const_iterator end = resp.end();
        for (std::sregex_iterator it(start, end, xml_regex); it != std::sregex_iterator(); ++it) {
            json tc;
            tc["id"] = "tool_" + std::to_string(msg.tool_calls.size());
            tc["type"] = "function";
            tc["function"]["name"] = (*it)[1].str();
            tc["function"]["arguments"] = (*it)[2].str();
            msg.tool_calls.push_back(tc);
        }
    } else {
        // Try to parse JSON-style tool calls (OpenAI/Anthropic format)
        size_t pos = resp.find("\"tool_calls\"");
        if (pos == std::string::npos) pos = resp.find("\"tool_use\"");
        if (pos != std::string::npos) {
            try {
                auto j = json::parse(resp);
                if (j.contains("tool_calls")) {
                    msg.tool_calls = j["tool_calls"];
                }
            } catch (...) {}
        }
    }

    return msg;
}

std::string Agent::execute_tool_calls(const std::vector<ToolCall>& calls) {
    auto& tools = ModuleRegistry::instance().require<Tools>();
    auto results = tools.execute_batch(calls);

    std::string output;
    for (auto& r : results) {
        output += "[" + r.call_id + "] " + (r.is_error ? "ERROR: " + r.error_message : r.content) + "\n";
    }
    return output;
}

// ── Background sub-agents ─────────────────────────────────────────
std::string Agent::launch_background_sub_agent(const AgentConfig& config, std::string_view task) {
    auto task_id = "bg_agent_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());

    auto bg = std::make_shared<BackgroundAgent>();
    bg->id = task_id;
    bg->config = config;
    bg->task = std::string(task);

    auto promise = std::make_shared<std::promise<AgentResult>>();
    bg->future = promise->get_future().share();

    std::thread([this, bg, promise]() {
        try {
            auto result = execute_sub_agent(bg->config, bg->task);
            if (!bg->cancelled.load()) {
                promise->set_value(result);
                auto& notice = ModuleRegistry::instance().require<Notice>();
                notice.background_completed(bg->id, result.output);
            }
        } catch (const std::exception& e) {
            AgentResult err;
            err.success = false;
            err.error = e.what();
            promise->set_value(err);
        }
    }).detach();

    {
        std::lock_guard lock(m_bg_mutex);
        m_bg_agents[task_id] = bg;
    }

    return task_id;
}

std::shared_future<AgentResult> Agent::get_background_result(std::string_view agent_id) {
    std::lock_guard lock(m_bg_mutex);
    auto it = m_bg_agents.find(std::string(agent_id));
    if (it == m_bg_agents.end()) {
        std::promise<AgentResult> p;
        AgentResult r;
        r.success = false;
        r.error = "Background agent not found: " + std::string(agent_id);
        p.set_value(r);
        return p.get_future().share();
    }
    return it->second->future;
}

void Agent::cancel_background_agent(std::string_view agent_id) {
    std::lock_guard lock(m_bg_mutex);
    auto it = m_bg_agents.find(std::string(agent_id));
    if (it != m_bg_agents.end()) {
        it->second->cancelled.store(true);
    }
}

Agent::MPCState Agent::get_mpc_state() const {
    return m_state;
}

void Agent::reset_mpc_state() {
    m_state = {};
}

} // namespace agent
