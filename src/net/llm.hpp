#pragma once
#include "core/common.hpp"
#include "core/crypto.hpp"
#include "net/http_client.hpp"

namespace LLMProviders
{
    // ==================== 共享工具 ====================

    // OpenAI 兼容请求头：无 API Key 时不发送 Authorization
    inline std::vector<std::string> build_headers(const std::string &api_key)
    {
        std::vector<std::string> headers = {"Content-Type: application/json"};
        if (!api_key.empty())
            headers.push_back("Authorization: Bearer " + api_key);
        return headers;
    }

    // 规范化 base_url：去掉末尾斜杠，避免拼出 "//v1/models" 双斜杠路径导致 404
    inline std::string normalize_base_url(const std::string &base_url)
    {
        std::string url = base_url;
        while (url.size() > 1 && url.back() == '/')
            url.pop_back();
        return url;
    }

    // 角色规范化：自定义 user_name / agent_name 映射为标准 user / assistant
    inline void normalize_roles(nlohmann::json &request)
    {
        if (!request.contains("messages") || !request["messages"].is_array())
            return;
        auto user_name = run_unit::settings["user_name"].get_ref<const std::string &>();
        auto agent_name = run_unit::settings["agent_name"].get_ref<const std::string &>();
        for (auto &msg : request["messages"])
        {
            if (!msg.contains("role") || !msg["role"].is_string())
                continue;
            auto &role = msg["role"].get_ref<std::string &>();
            if (role == user_name)
                role = "user";
            else if (role == agent_name)
                role = "assistant";
        }
    }

    // 请求规范化：角色映射 + 清理非通用参数。
    // think 是部分网关专有参数，false 等同默认值，移除以免严格接口（如 OpenAI 官方）报 400
    inline void sanitize_request(nlohmann::json &request)
    {
        normalize_roles(request);
        if (request.contains("think") && request["think"].is_boolean() && !request["think"].get<bool>())
            request.erase("think");
    }

    // 共享的 OpenAI 兼容 SSE 流式解析（OpenAI / Ollama / llama.cpp / 各类网关通用）
    inline std::string stream_chat_completions(
        CURL *curl,
        const std::string &url,
        const std::string &post_data,
        const std::vector<std::string> &headers,
        std::function<void(const std::string &)> on_token,
        std::function<void(const std::string &)> on_thinking = nullptr)
    {
        std::string accumulated;
        std::string buffer;
        std::string raw;
        bool debug_raw = std::getenv("AGENT_DEBUG_HTTP") != nullptr;

        auto parse_sse = [&](const char *data, size_t len)
        {
            if (debug_raw)
                raw.append(data, len);
            buffer.append(data, len);
            size_t pos = 0;
            while (true)
            {
                size_t nl = buffer.find('\n', pos);
                if (nl == std::string::npos)
                    break;
                std::string line = buffer.substr(pos, nl - pos);
                pos = nl + 1;

                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                if (line.empty())
                    continue;

                if (line.find("data: ") == 0)
                {
                    std::string payload = line.substr(6);
                    if (payload == "[DONE]")
                        continue;
                    try
                    {
                        auto j = nlohmann::json::parse(payload);
                        if (!j.contains("choices") || j["choices"].empty())
                            continue;
                        auto &delta = j["choices"][0]["delta"];

                        if (delta.contains("reasoning_content") && on_thinking)
                        {
                            std::string rc = delta["reasoning_content"].get<std::string>();
                            if (!rc.empty())
                                on_thinking(rc);
                        }
                        if (delta.contains("content"))
                        {
                            std::string c = delta["content"].get<std::string>();
                            accumulated += c;
                            if (on_token)
                                on_token(c);
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
            buffer = buffer.substr(pos);
        };

        net_unit::CURL_stream_post(curl, url.c_str(), post_data, headers, parse_sse);
        if (debug_raw && accumulated.empty())
        {
            std::cerr << "DEBUG - empty stream from " << url << "\n";
            try
            {
                auto req = nlohmann::json::parse(post_data);
                std::string roles;
                for (auto &m : req["messages"])
                    roles += m.value("role", "?") + " ";
                std::cerr << "  roles: [" << roles << "]\n";
            }
            catch (...)
            {
            }
            std::cerr << "  response: " << raw.substr(0, 600) << std::endl;
        }
        return accumulated;
    }

    // OpenAI 兼容 /v1/models 响应 → WebUI 需要的 {data:[{id,status}]} 格式。
    // 服务端带真实 status 时忠实保留（如 llama.cpp 的 loaded/unloaded），否则默认 loaded
    inline std::string normalize_models_response(const std::string &raw)
    {
        nlohmann::json data = nlohmann::json::array();
        try
        {
            auto j = nlohmann::json::parse(raw);
            nlohmann::json items = j.contains("data") ? j["data"] : j.value("models", nlohmann::json::array());
            for (auto &item : items)
            {
                std::string id = item.value("id", item.value("model", ""));
                if (id.empty())
                    continue;
                std::string status = "loaded";
                if (item.contains("status") && item["status"].is_object() &&
                    item["status"].contains("value") && item["status"]["value"].is_string())
                    status = item["status"]["value"].get<std::string>();
                data.push_back({{"id", id}, {"status", {{"value", status}}}});
            }
        }
        catch (...)
        {
        }
        return nlohmann::json({{"data", data}}).dump();
    }

    class LlamaClient
    {
    private:
        std::string base_url_;
        crypto_unit::SecureString api_key_;
        CURL *curl_ = curl_easy_init();

    public:
        explicit LlamaClient(const std::string &base_url = "http://localhost:8080", const std::string &api_key = "")
            : base_url_(normalize_base_url(base_url)), api_key_(api_key) {}
        void set_api_key(const std::string &api_key) { api_key_.set(api_key); }
        void set_base_url(const std::string &base_url) { base_url_ = normalize_base_url(base_url); }
        ~LlamaClient()
        {
            if (curl_)
                curl_easy_cleanup(curl_);
        }

        // llama.cpp 服务器模型管理接口：POST /models/unload
        bool unload_model(std::string &model)
        {
            std::string buf;
            std::string url = base_url_ + "/models/unload";
            nlohmann::json request = {{"model", model}};
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, "Content-Type: application/json"))
                return false;
            return true;
        }

        // llama.cpp OpenAI 兼容接口：POST /v1/chat/completions
        bool generate(nlohmann::json &request, nlohmann::json &response)
        {
            sanitize_request(request);
            std::string buf;
            std::string url = base_url_ + "/v1/chat/completions";
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, build_headers(api_key_.str())))
                return false;
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // llama.cpp 支持 OpenAI 兼容 SSE 流式生成
        std::string stream_generate(
            nlohmann::json &request,
            std::function<void(const std::string &)> on_token,
            std::function<void(const std::string &)> on_thinking = nullptr)
        {
            sanitize_request(request);
            request["stream"] = true;
            std::string url = base_url_ + "/v1/chat/completions";
            return stream_chat_completions(curl_, url, request.dump(), build_headers(api_key_.str()),
                                           on_token, on_thinking);
        }

        // llama.cpp OpenAI 兼容接口：GET /v1/models（仅列出已加载模型）
        std::string models()
        {
            std::string result;
            std::string url = base_url_ + "/v1/models";
            if (!net_unit::CURL_get(curl_, url.c_str(), result, build_headers(api_key_.str())))
                return "{\"data\":[]}";
            return normalize_models_response(result);
        }
    };

    class OllamaClient
    {
    private:
        std::string base_url_;
        crypto_unit::SecureString api_key_;
        CURL *curl_ = curl_easy_init();

    public:
        explicit OllamaClient(const std::string &base_url = "http://localhost:11434", const std::string &api_key = "")
            : base_url_(normalize_base_url(base_url)), api_key_(api_key) {}
        void set_api_key(const std::string &api_key) { api_key_.set(api_key); }
        void set_base_url(const std::string &base_url) { base_url_ = normalize_base_url(base_url); }
        ~OllamaClient()
        {
            if (curl_)
                curl_easy_cleanup(curl_);
        }

        // Ollama OpenAI 兼容接口：POST /v1/chat/completions
        bool generate(nlohmann::json &request, nlohmann::json &response)
        {
            sanitize_request(request);
            std::string buf;
            std::string url = base_url_ + "/v1/chat/completions";
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, build_headers(api_key_.str())))
                return false;
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // Ollama OpenAI 兼容接口支持 SSE 流式生成
        std::string stream_generate(
            nlohmann::json &request,
            std::function<void(const std::string &)> on_token,
            std::function<void(const std::string &)> on_thinking = nullptr)
        {
            sanitize_request(request);
            request["stream"] = true;
            std::string url = base_url_ + "/v1/chat/completions";
            return stream_chat_completions(curl_, url, request.dump(), build_headers(api_key_.str()),
                                           on_token, on_thinking);
        }

        // Ollama 原生接口：GET /api/tags 列出全部本地模型，/api/ps 标注已加载状态
        std::string models()
        {
            std::string tags;
            std::string url = base_url_ + "/api/tags";
            if (!net_unit::CURL_get(curl_, url.c_str(), tags))
                return "{\"data\":[]}";

            std::vector<std::string> loaded;
            std::string ps;
            if (net_unit::CURL_get(curl_, (base_url_ + "/api/ps").c_str(), ps))
            {
                try
                {
                    auto psj = nlohmann::json::parse(ps);
                    for (auto &m : psj.value("models", nlohmann::json::array()))
                        loaded.push_back(m.value("model", ""));
                }
                catch (...)
                {
                }
            }

            nlohmann::json data = nlohmann::json::array();
            try
            {
                auto tagsj = nlohmann::json::parse(tags);
                for (auto &item : tagsj.value("models", nlohmann::json::array()))
                {
                    std::string id = item.value("model", item.value("name", ""));
                    if (id.empty())
                        continue;
                    bool is_loaded = loaded.empty() ||
                                     std::find(loaded.begin(), loaded.end(), id) != loaded.end();
                    data.push_back({{"id", id}, {"status", {{"value", is_loaded ? "loaded" : "idle"}}}});
                }
            }
            catch (...)
            {
            }
            return nlohmann::json({{"data", data}}).dump();
        }
    };

    class OpenAIClient
    {
    private:
        std::string base_url_;
        crypto_unit::SecureString api_key_;
        CURL *curl_ = curl_easy_init();

    public:
        explicit OpenAIClient(const std::string &base_url = "https://api.openai.com", const std::string &api_key = "")
            : base_url_(normalize_base_url(base_url)), api_key_(api_key) {}
        void set_api_key(const std::string &api_key) { api_key_.set(api_key); }
        void set_base_url(const std::string &base_url) { base_url_ = normalize_base_url(base_url); }
        ~OpenAIClient()
        {
            if (curl_)
                curl_easy_cleanup(curl_);
        }

        // OpenAI Chat Completions：POST /v1/chat/completions
        bool generate(nlohmann::json &request, nlohmann::json &response)
        {
            sanitize_request(request);
            std::string buf;
            std::string url = base_url_ + "/v1/chat/completions";
            if (!net_unit::CURL_post(curl_, url.c_str(), request.dump(), buf, build_headers(api_key_.str())))
                return false;
            try
            {
                response = nlohmann::json::parse(buf);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        // 流式生成：OpenAI SSE（delta.content 内容增量，delta.reasoning_content 推理增量）
        std::string stream_generate(
            nlohmann::json &request,
            std::function<void(const std::string &)> on_token,
            std::function<void(const std::string &)> on_thinking = nullptr)
        {
            sanitize_request(request);
            request["stream"] = true;
            std::string url = base_url_ + "/v1/chat/completions";
            return stream_chat_completions(curl_, url, request.dump(), build_headers(api_key_.str()),
                                           on_token, on_thinking);
        }

        // OpenAI Models：GET /v1/models
        std::string models()
        {
            std::string result;
            std::string url = base_url_ + "/v1/models";
            if (!net_unit::CURL_get(curl_, url.c_str(), result, build_headers(api_key_.str())))
                return "{\"data\":[]}";
            return normalize_models_response(result);
        }
    };
} // namespace LLMProviders
