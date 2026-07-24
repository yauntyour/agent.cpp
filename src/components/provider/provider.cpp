#include "components/provider/provider.hpp"
#include "utils/http.hpp"
#include "core/exception.hpp"
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

void Provider::on_initialize() {
    auto& cfg = Config::instance();
    for (auto& pc : cfg.providers) {
        ProviderInfo info;
        info.id = pc.name;
        info.name = pc.name;
        info.base_url = pc.api_base;
        info.available = true;
        m_providers.push_back(info);
    }
    m_current_provider_id = cfg.default_provider;
    m_current_model = cfg.default_model;
}

void Provider::on_shutdown() {}

// ── Provider management ───────────────────────────────────────────
std::vector<Provider::ProviderInfo> Provider::list_providers() { return m_providers; }

void Provider::add_provider(const ProviderInfo& info) {
    m_providers.push_back(info);
}

void Provider::remove_provider(std::string_view id) {
    m_providers.erase(std::remove_if(m_providers.begin(), m_providers.end(),
        [&](auto& p) { return p.id == id; }), m_providers.end());
}

void Provider::set_current(std::string_view id) { m_current_provider_id = id; }

std::vector<Provider::ModelInfo> Provider::list_models(std::string_view provider_id) {
    // Default model list — can be extended by querying provider API
    std::vector<ModelInfo> models = {
        {"gpt-4o", "GPT-4o", 128000, true, true, true},
        {"gpt-4o-mini", "GPT-4o Mini", 128000, true, true, true},
        {"gpt-4-turbo", "GPT-4 Turbo", 128000, true, true, true},
        {"claude-3.5-sonnet", "Claude 3.5 Sonnet", 200000, true, true, true},
        {"claude-3-opus", "Claude 3 Opus", 200000, true, true, true},
        {"claude-3-haiku", "Claude 3 Haiku", 200000, true, true, true},
        {"llama-3.1-70b", "Llama 3.1 70B", 128000, false, true, true},
        {"llama-3.1-8b", "Llama 3.1 8B", 128000, false, true, true},
    };
    return models;
}

Provider::ModelInfo Provider::current_model() {
    auto models = list_models(m_current_provider_id);
    for (auto& m : models) {
        if (m.id == m_current_model) return m;
    }
    return models.empty() ? ModelInfo{} : models[0];
}

void Provider::set_model(std::string_view model_id) { m_current_model = model_id; }

// ── Token counting (approximate) ──────────────────────────────────
size_t Provider::count_tokens(std::string_view text) {
    return text.size() / 4; // Rough estimate: ~4 chars per token
}

size_t Provider::count_message_tokens(const ChatMessage& msg) {
    size_t tokens = 20; // Overhead per message
    tokens += count_tokens(msg.content);
    if (!msg.name.empty()) tokens += count_tokens(msg.name);
    return tokens;
}

// ── Configuration ─────────────────────────────────────────────────
int Provider::context_length() {
    auto* cfg = Config::instance().current_provider();
    return cfg ? cfg->context_length : 128000;
}

void Provider::set_context_length(int tokens) {
    auto* cfg = Config::instance().current_provider();
    if (cfg) cfg->context_length = tokens;
}

float Provider::temperature() {
    auto* cfg = Config::instance().current_provider();
    return cfg ? cfg->temperature : 0.7f;
}

void Provider::set_temperature(float temp) {
    auto* cfg = Config::instance().current_provider();
    if (cfg) cfg->temperature = temp;
}

void Provider::set_thinking_mode(std::string_view mode) {
    auto* cfg = Config::instance().current_provider();
    if (cfg) cfg->thinking_mode = mode;
}

void Provider::set_thinking_budget(int tokens) {
    auto* cfg = Config::instance().current_provider();
    if (cfg) cfg->thinking_budget = tokens;
}

bool Provider::supports_vision() {
    return current_model().supports_vision;
}

// ── Main generation methods ───────────────────────────────────────
std::string Provider::generate(const std::vector<ChatMessage>& messages,
                                const std::vector<ToolDefinition>& tools,
                                const json& extra) {
    auto* cfg = Config::instance().current_provider();
    if (!cfg) {
        LOG_ERROR("Provider", "No provider configured");
        throw ProviderException(ErrorCode::NoProviderConfig, "No provider configured");
    }

    std::string type = cfg->type;
    if (type.empty()) {
        if (cfg->api_base.find("anthropic") != std::string::npos) type = "anthropic";
        else if (cfg->api_base.find("ollama") != std::string::npos ||
                 cfg->api_base.find("11434") != std::string::npos) type = "ollama";
        else if (cfg->api_base.find("8080") != std::string::npos) type = "llama_server";
        else type = "openai";
    }

    LOG_DEBUG("Provider", "Generating with provider type: " + type + ", model: " + cfg->model);

    if (type == "anthropic") return generate_anthropic(messages, tools, extra, *cfg);
    if (type == "ollama") return generate_ollama(messages, tools, extra, *cfg);
    if (type == "llama_server") return generate_llama_server(messages, tools, extra, *cfg);
    return generate_openai(messages, tools, extra, *cfg);
}

void Provider::generate_stream(const std::vector<ChatMessage>& messages,
                               StreamCallback callback,
                               const std::vector<ToolDefinition>& tools,
                               const json& extra) {
    auto* cfg = Config::instance().current_provider();
    if (!cfg) {
        LOG_ERROR("Provider", "No provider configured for streaming");
        throw ProviderException(ErrorCode::NoProviderConfig, "No provider configured");
    }

    std::string type = cfg->type;
    if (type.empty()) {
        if (cfg->api_base.find("anthropic") != std::string::npos) type = "anthropic";
        else if (cfg->api_base.find("ollama") != std::string::npos) type = "ollama";
        else if (cfg->api_base.find("8080") != std::string::npos) type = "llama_server";
        else type = "openai";
    }

    LOG_DEBUG("Provider", "Streaming with provider type: " + type + ", model: " + cfg->model);

    if (type == "anthropic") generate_anthropic_stream(messages, callback, tools, extra, *cfg);
    else if (type == "ollama") generate_ollama_stream(messages, callback, tools, extra, *cfg);
    else if (type == "llama_server") generate_llama_server_stream(messages, callback, tools, extra, *cfg);
    else generate_openai_stream(messages, callback, tools, extra, *cfg);
}

// ── OpenAI provider ───────────────────────────────────────────────
std::string Provider::generate_openai(const std::vector<ChatMessage>& messages,
                                       const std::vector<ToolDefinition>& tools,
                                       const json& extra, const Config::ProviderConfig& cfg) {
    json body;
    body["model"] = cfg.model;
    body["temperature"] = cfg.temperature;
    body["top_p"] = cfg.top_p;
    body["max_tokens"] = cfg.max_tokens;

    json msgs = json::array();
    for (auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (!msg.name.empty()) m["name"] = msg.name;
        if (!msg.tool_call_id.empty()) m["tool_call_id"] = msg.tool_call_id;
        if (!msg.tool_calls.empty()) m["tool_calls"] = msg.tool_calls;
        msgs.push_back(m);
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        json tl = json::array();
        for (auto& t : tools) {
            json tj;
            tj["type"] = "function";
            tj["function"]["name"] = t.name;
            tj["function"]["description"] = t.description;
            tj["function"]["parameters"] = t.parameters;
            tl.push_back(tj);
        }
        body["tools"] = tl;
        body["tool_choice"] = "auto";
    }

    if (cfg.thinking_mode == "enabled") {
        body["reasoning_effort"] = cfg.thinking_budget > 0 ?
            (cfg.thinking_budget >= 16000 ? "high" : "medium") : "low";
    }

    // Merge extra params
    if (!extra.empty()) {
        for (auto& [k, v] : extra.items()) {
            body[k] = v;
        }
    }

    std::string url = cfg.api_base;
    if (!url.ends_with('/')) url += '/';
    url += "chat/completions";

    auto& client = http::HttpClient::instance();
    auto resp = client.post(url, body.dump(), {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + cfg.api_key}
    });

    if (!resp.ok()) {
        LOG_ERROR("Provider", "OpenAI API error: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
        throw ApiException("OpenAI API error: " + resp.error + " body: " + resp.body, resp.status_code);
    }

    try {
        auto j = json::parse(resp.body);
        return j["choices"][0]["message"]["content"].get<std::string>();
    } catch (const json::parse_error& e) {
        LOG_ERROR("Provider", std::string("Failed to parse OpenAI response: ") + e.what());
        throw ProviderException(ErrorCode::InvalidApiResponse, "Failed to parse OpenAI response: " + std::string(e.what()));
    } catch (const json::out_of_range& e) {
        LOG_ERROR("Provider", std::string("Unexpected OpenAI response structure: ") + e.what());
        throw ProviderException(ErrorCode::InvalidApiResponse, "Unexpected OpenAI response structure: " + std::string(e.what()));
    }
}

void Provider::generate_openai_stream(const std::vector<ChatMessage>& messages,
                                       StreamCallback callback,
                                       const std::vector<ToolDefinition>& tools,
                                       const json& extra,
                                       const Config::ProviderConfig& cfg) {
    json body;
    body["model"] = cfg.model;
    body["temperature"] = cfg.temperature;
    body["top_p"] = cfg.top_p;
    body["max_tokens"] = cfg.max_tokens;
    body["stream"] = true;

    json msgs = json::array();
    for (auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        if (!msg.name.empty()) m["name"] = msg.name;
        if (!msg.tool_call_id.empty()) m["tool_call_id"] = msg.tool_call_id;
        if (!msg.tool_calls.empty()) m["tool_calls"] = msg.tool_calls;
        msgs.push_back(m);
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        json tl = json::array();
        for (auto& t : tools) {
            json tj;
            tj["type"] = "function";
            tj["function"]["name"] = t.name;
            tj["function"]["description"] = t.description;
            tj["function"]["parameters"] = t.parameters;
            tl.push_back(tj);
        }
        body["tools"] = tl;
    }

    if (!extra.empty()) {
        for (auto& [k, v] : extra.items()) {
            body[k] = v;
        }
    }

    std::string url = cfg.api_base;
    if (!url.ends_with('/')) url += '/';
    url += "chat/completions";

    http::HttpRequest req;
    req.url = url;
    req.method = "POST";
    req.body = body.dump();
    req.headers = {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + cfg.api_key}
    };
    req.timeout_sec = Config::instance().request_timeout_sec;

    auto& client = http::HttpClient::instance();
    auto resp = client.request(req);

    if (!resp.ok()) {
        StreamingChunk err_chunk;
        err_chunk.finished = true;
        err_chunk.finish_reason = "error";
        err_chunk.content = "API error: " + resp.error + " - " + resp.body;
        callback(err_chunk);
        return;
    }

    http::SSEParser parser;
    parser.feed(resp.body, [&](std::string_view event, std::string_view data) {
        if (data == "[DONE]") {
            StreamingChunk chunk;
            chunk.finished = true;
            chunk.finish_reason = "stop";
            callback(chunk);
            return;
        }

        try {
            auto j = json::parse(data);
            auto& choices = j["choices"];
            if (choices.empty()) return;

            StreamingChunk chunk;

            if (choices[0].contains("delta")) {
                auto& delta = choices[0]["delta"];
                if (delta.contains("content")) {
                    chunk.content = delta["content"].get<std::string>();
                }
                if (delta.contains("reasoning_content")) {
                    chunk.reasoning_content = delta["reasoning_content"].get<std::string>();
                }
                if (delta.contains("tool_calls")) {
                    for (auto& tc : delta["tool_calls"]) {
                        if (tc.contains("id")) chunk.tool_call_id = tc["id"].get<std::string>();
                        if (tc.contains("function")) {
                            if (tc["function"].contains("name"))
                                chunk.tool_call_name = tc["function"]["name"].get<std::string>();
                            if (tc["function"].contains("arguments"))
                                chunk.tool_call_arguments = tc["function"]["arguments"].get<std::string>();
                        }
                    }
                }
            }

            if (choices[0].contains("finish_reason") && !choices[0]["finish_reason"].is_null()) {
                chunk.finished = true;
                chunk.finish_reason = choices[0]["finish_reason"].get<std::string>();
            }

            if (j.contains("usage")) {
                chunk.usage = j["usage"];
            }

            if (!chunk.content.empty() || !chunk.reasoning_content.empty() ||
                !chunk.tool_call_name.empty() || chunk.finished) {
                callback(chunk);
            }
        } catch (const json::parse_error& e) {
            LOG_DEBUG("Provider", std::string("Skipping malformed SSE chunk: ") + e.what());
        } catch (const json::out_of_range& e) {
            LOG_DEBUG("Provider", std::string("Skipping SSE chunk with missing fields: ") + e.what());
        } catch (const std::exception& e) {
            LOG_DEBUG("Provider", std::string("Error processing SSE chunk: ") + e.what());
        }
    });
}

// ── Anthropic (Claude) provider ───────────────────────────────────
std::string Provider::generate_anthropic(const std::vector<ChatMessage>& messages,
                                          const std::vector<ToolDefinition>& tools,
                                          const json& extra, const Config::ProviderConfig& cfg) {
    json body;
    body["model"] = cfg.model;
    body["max_tokens"] = cfg.max_tokens;
    body["temperature"] = cfg.temperature;

    if (cfg.thinking_mode == "enabled") {
        body["thinking"]["type"] = "enabled";
        body["thinking"]["budget_tokens"] = cfg.thinking_budget;
    }

    json msgs = json::array();
    json system_prompt = json::array();

    for (auto& msg : messages) {
        if (msg.role == "system") {
            json s;
            s["type"] = "text";
            s["text"] = msg.content;
            system_prompt.push_back(s);
        } else {
            json m;
            m["role"] = msg.role;
            m["content"] = json::array({json{{"type", "text"}, {"text", msg.content}}});
            msgs.push_back(m);
        }
    }

    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }
    body["messages"] = msgs;

    if (!tools.empty()) {
        json tl = json::array();
        for (auto& t : tools) {
            json tj;
            tj["name"] = t.name;
            tj["description"] = t.description;
            tj["input_schema"] = t.parameters;
            tl.push_back(tj);
        }
        body["tools"] = tl;
    }

    if (!extra.empty()) {
        for (auto& [k, v] : extra.items()) {
            body[k] = v;
        }
    }

    std::string url = cfg.api_base;
    if (!url.ends_with('/')) url += '/';
    url += "messages";

    auto& client = http::HttpClient::instance();
    auto resp = client.post(url, body.dump(), {
        {"Content-Type", "application/json"},
        {"x-api-key", cfg.api_key},
        {"anthropic-version", "2023-06-01"}
    });

    if (!resp.ok()) {
        LOG_ERROR("Provider", "Anthropic API error: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
        throw ApiException("Anthropic API error: " + resp.error + " body: " + resp.body, resp.status_code);
    }

    try {
        auto j = json::parse(resp.body);
        for (auto& c : j["content"]) {
            if (c["type"] == "text") {
                return c["text"].get<std::string>();
            }
        }
        return "";
    } catch (const json::parse_error& e) {
        LOG_ERROR("Provider", std::string("Failed to parse Anthropic response: ") + e.what());
        throw ProviderException(ErrorCode::InvalidApiResponse, "Failed to parse Anthropic response: " + std::string(e.what()));
    } catch (const json::out_of_range& e) {
        LOG_ERROR("Provider", std::string("Unexpected Anthropic response structure: ") + e.what());
        throw ProviderException(ErrorCode::InvalidApiResponse, "Unexpected Anthropic response structure: " + std::string(e.what()));
    }
}

void Provider::generate_anthropic_stream(const std::vector<ChatMessage>& messages,
                                          StreamCallback callback,
                                          const std::vector<ToolDefinition>& tools,
                                          const json& extra,
                                          const Config::ProviderConfig& cfg) {
    json body;
    body["model"] = cfg.model;
    body["max_tokens"] = cfg.max_tokens;
    body["temperature"] = cfg.temperature;
    body["stream"] = true;

    if (cfg.thinking_mode == "enabled") {
        body["thinking"]["type"] = "enabled";
        body["thinking"]["budget_tokens"] = cfg.thinking_budget;
    }

    json msgs = json::array();
    json system_prompt = json::array();
    for (auto& msg : messages) {
        if (msg.role == "system") {
            json s;
            s["type"] = "text";
            s["text"] = msg.content;
            system_prompt.push_back(s);
        } else {
            json m;
            m["role"] = msg.role;
            m["content"] = json::array({json{{"type", "text"}, {"text", msg.content}}});
            msgs.push_back(m);
        }
    }
    if (!system_prompt.empty()) body["system"] = system_prompt;
    body["messages"] = msgs;

    if (!tools.empty()) {
        json tl = json::array();
        for (auto& t : tools) {
            json tj;
            tj["name"] = t.name;
            tj["description"] = t.description;
            tj["input_schema"] = t.parameters;
            tl.push_back(tj);
        }
        body["tools"] = tl;
    }

    if (!extra.empty()) {
        for (auto& [k, v] : extra.items()) body[k] = v;
    }

    std::string url = cfg.api_base;
    if (!url.ends_with('/')) url += '/';
    url += "messages";

    http::HttpRequest req;
    req.url = url;
    req.method = "POST";
    req.body = body.dump();
    req.headers = {
        {"Content-Type", "application/json"},
        {"x-api-key", cfg.api_key},
        {"anthropic-version", "2023-06-01"}
    };
    req.timeout_sec = Config::instance().request_timeout_sec;

    auto& client = http::HttpClient::instance();
    auto resp = client.request(req);

    if (!resp.ok()) {
        StreamingChunk err;
        err.finished = true;
        err.finish_reason = "error";
        err.content = "API error: " + resp.error;
        callback(err);
        return;
    }

    http::SSEParser parser;
    parser.feed(resp.body, [&](std::string_view event, std::string_view data) {
        try {
            auto j = json::parse(data);
            StreamingChunk chunk;

            if (j.contains("type")) {
                auto type = j["type"].get<std::string>();
                if (type == "content_block_delta") {
                    auto& delta = j["delta"];
                    if (delta["type"] == "text_delta") {
                        chunk.content = delta["text"].get<std::string>();
                    } else if (delta["type"] == "thinking_delta") {
                        chunk.reasoning_content = delta["thinking"].get<std::string>();
                    }
                } else if (type == "message_stop") {
                    chunk.finished = true;
                    chunk.finish_reason = "stop";
                }
                if (j.contains("usage")) chunk.usage = j["usage"];
            }

            if (!chunk.content.empty() || !chunk.reasoning_content.empty() || chunk.finished) {
                callback(chunk);
            }
        } catch (const json::parse_error& e) {
            LOG_DEBUG("Provider", std::string("Skipping malformed Anthropic SSE chunk: ") + e.what());
        } catch (const json::out_of_range& e) {
            LOG_DEBUG("Provider", std::string("Skipping Anthropic SSE chunk with missing fields: ") + e.what());
        } catch (const std::exception& e) {
            LOG_DEBUG("Provider", std::string("Error processing Anthropic SSE chunk: ") + e.what());
        }
    });
}

// ── Ollama provider ───────────────────────────────────────────────
std::string Provider::generate_ollama(const std::vector<ChatMessage>& messages,
                                       const std::vector<ToolDefinition>& tools,
                                       const json& extra, const Config::ProviderConfig& cfg) {
    json body;
    body["model"] = cfg.model;

    json msgs = json::array();
    for (auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgs.push_back(m);
    }
    body["messages"] = msgs;
    body["stream"] = false;

    json options;
    options["temperature"] = cfg.temperature;
    options["top_p"] = cfg.top_p;
    options["num_predict"] = cfg.max_tokens;
    body["options"] = options;

    if (!extra.empty()) {
        for (auto& [k, v] : extra.items()) body[k] = v;
    }

    std::string url = cfg.api_base;
    if (!url.ends_with('/')) url += '/';
    url += "api/chat";

    auto& client = http::HttpClient::instance();
    auto resp = client.post(url, body.dump(), {
        {"Content-Type", "application/json"}
    });

    if (!resp.ok()) {
        LOG_ERROR("Provider", "Ollama API error: " + resp.error + " (HTTP " + std::to_string(resp.status_code) + ")");
        throw ApiException("Ollama API error: " + resp.error + " body: " + resp.body, resp.status_code);
    }

    try {
        auto j = json::parse(resp.body);
        return j["message"]["content"].get<std::string>();
    } catch (const json::parse_error& e) {
        LOG_ERROR("Provider", std::string("Failed to parse Ollama response: ") + e.what());
        throw ProviderException(ErrorCode::InvalidApiResponse, "Failed to parse Ollama response: " + std::string(e.what()));
    } catch (const json::out_of_range& e) {
        LOG_ERROR("Provider", std::string("Unexpected Ollama response structure: ") + e.what());
        throw ProviderException(ErrorCode::InvalidApiResponse, "Unexpected Ollama response structure: " + std::string(e.what()));
    }
}

void Provider::generate_ollama_stream(const std::vector<ChatMessage>& messages,
                                       StreamCallback callback,
                                       const std::vector<ToolDefinition>& tools,
                                       const json& extra,
                                       const Config::ProviderConfig& cfg) {
    json body;
    body["model"] = cfg.model;

    json msgs = json::array();
    for (auto& msg : messages) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        msgs.push_back(m);
    }
    body["messages"] = msgs;
    body["stream"] = true;

    json options;
    options["temperature"] = cfg.temperature;
    options["num_predict"] = cfg.max_tokens;
    body["options"] = options;

    if (!extra.empty()) {
        for (auto& [k, v] : extra.items()) body[k] = v;
    }

    std::string url = cfg.api_base;
    if (!url.ends_with('/')) url += '/';
    url += "api/chat";

    auto& client = http::HttpClient::instance();
    auto resp = client.post(url, body.dump(), {
        {"Content-Type", "application/json"}
    });

    if (!resp.ok()) {
        StreamingChunk err;
        err.finished = true;
        err.content = "API error: " + resp.error;
        callback(err);
        return;
    }

    // Ollama streams NDJSON (one JSON per line)
    std::stringstream ss(resp.body);
    std::string line;
    while (std::getline(ss, line)) {
        if (line.empty()) continue;
        try {
            auto j = json::parse(line);
            StreamingChunk chunk;
            if (j.contains("message") && j["message"].contains("content")) {
                chunk.content = j["message"]["content"].get<std::string>();
            }
            if (j["done"].get<bool>()) {
                chunk.finished = true;
                chunk.finish_reason = "stop";
            }
            callback(chunk);
        } catch (const json::parse_error& e) {
            LOG_DEBUG("Provider", std::string("Skipping malformed Ollama NDJSON line: ") + e.what());
        } catch (const json::out_of_range& e) {
            LOG_DEBUG("Provider", std::string("Skipping Ollama NDJSON with missing fields: ") + e.what());
        } catch (const std::exception& e) {
            LOG_DEBUG("Provider", std::string("Error processing Ollama NDJSON line: ") + e.what());
        }
    }
}

// ── llama-server provider ─────────────────────────────────────────
std::string Provider::generate_llama_server(const std::vector<ChatMessage>& messages,
                                             const std::vector<ToolDefinition>& tools,
                                             const json& extra, const Config::ProviderConfig& cfg) {
    // llama-server also uses OpenAI-compatible API
    return generate_openai(messages, tools, extra, cfg);
}

void Provider::generate_llama_server_stream(const std::vector<ChatMessage>& messages,
                                             StreamCallback callback,
                                             const std::vector<ToolDefinition>& tools,
                                             const json& extra,
                                             const Config::ProviderConfig& cfg) {
    generate_openai_stream(messages, callback, tools, extra, cfg);
}

} // namespace agent
