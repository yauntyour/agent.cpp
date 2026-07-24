#pragma once
#include "core/module.hpp"
#include "core/config.hpp"
#include "utils/crypto.hpp"
#include "utils/http.hpp"
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <nlohmann/json.hpp>

namespace agent {

struct ChatMessage {
    std::string role;       // "system", "user", "assistant", "tool"
    std::string content;
    std::string name;
    std::string tool_call_id;
    std::vector<nlohmann::json> tool_calls;
    bool is_media = false;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    nlohmann::json parameters;  // JSON Schema
};

struct StreamingChunk {
    std::string content;
    std::string reasoning_content;
    std::string tool_call_id;
    std::string tool_call_name;
    std::string tool_call_arguments;
    bool finished = false;
    std::string finish_reason;
    nlohmann::json usage;
};

using StreamCallback = std::function<void(const StreamingChunk&)>;
using GenerateCallback = std::function<void(std::string_view response, const nlohmann::json& usage)>;

class Provider : public Module<Provider> {
public:
    static constexpr std::string_view static_name() { return "provider"; }

    void on_initialize();
    void on_shutdown();

    // ── Provider management ────────────────────────────────────
    struct ProviderInfo {
        std::string id;
        std::string name;
        std::string base_url;
        std::string provider_type;  // "openai", "anthropic", "ollama", "llama_server"
        bool available = false;
    };

    std::vector<ProviderInfo> list_providers();
    void add_provider(const ProviderInfo& info);
    void remove_provider(std::string_view id);
    void set_current(std::string_view id);

    // ── Model management ───────────────────────────────────────
    struct ModelInfo {
        std::string id;
        std::string name;
        int context_length = 4096;
        bool supports_vision = false;
        bool supports_tools = false;
        bool supports_streaming = true;
    };

    std::vector<ModelInfo> list_models(std::string_view provider_id);
    ModelInfo current_model();
    void set_model(std::string_view model_id);

    // ── Token counting ─────────────────────────────────────────
    size_t count_tokens(std::string_view text);
    size_t count_message_tokens(const ChatMessage& msg);

    // ── Generation ─────────────────────────────────────────────
    std::string generate(const std::vector<ChatMessage>& messages,
                         const std::vector<ToolDefinition>& tools = {},
                         const nlohmann::json& extra = {});
    void generate_stream(const std::vector<ChatMessage>& messages,
                         StreamCallback callback,
                         const std::vector<ToolDefinition>& tools = {},
                         const nlohmann::json& extra = {});

    // ── Configuration ──────────────────────────────────────────
    int context_length();
    void set_context_length(int tokens);
    float temperature();
    void set_temperature(float temp);
    void set_thinking_mode(std::string_view mode);
    void set_thinking_budget(int tokens);
    bool supports_vision();

private:
    // ── Provider implementations ───────────────────────────────
    std::string generate_openai(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolDefinition>& tools,
                                const nlohmann::json& extra, const Config::ProviderConfig& cfg);
    void generate_openai_stream(const std::vector<ChatMessage>& messages,
                                StreamCallback callback,
                                const std::vector<ToolDefinition>& tools,
                                const nlohmann::json& extra, const Config::ProviderConfig& cfg);

    std::string generate_anthropic(const std::vector<ChatMessage>& messages,
                                   const std::vector<ToolDefinition>& tools,
                                   const nlohmann::json& extra, const Config::ProviderConfig& cfg);
    void generate_anthropic_stream(const std::vector<ChatMessage>& messages,
                                   StreamCallback callback,
                                   const std::vector<ToolDefinition>& tools,
                                   const nlohmann::json& extra, const Config::ProviderConfig& cfg);

    std::string generate_ollama(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolDefinition>& tools,
                                const nlohmann::json& extra, const Config::ProviderConfig& cfg);
    void generate_ollama_stream(const std::vector<ChatMessage>& messages,
                                StreamCallback callback,
                                const std::vector<ToolDefinition>& tools,
                                const nlohmann::json& extra, const Config::ProviderConfig& cfg);

    std::string generate_llama_server(const std::vector<ChatMessage>& messages,
                                      const std::vector<ToolDefinition>& tools,
                                      const nlohmann::json& extra, const Config::ProviderConfig& cfg);
    void generate_llama_server_stream(const std::vector<ChatMessage>& messages,
                                      StreamCallback callback,
                                      const std::vector<ToolDefinition>& tools,
                                      const nlohmann::json& extra, const Config::ProviderConfig& cfg);

    std::string m_current_provider_id = "default";
    std::string m_current_model;
    std::vector<ProviderInfo> m_providers;

};

} // namespace agent
