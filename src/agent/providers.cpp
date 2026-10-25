#include "agent/providers.hpp"

#include "core/crypto.hpp"
#include "core/log.hpp"
#include "agent/tools.hpp"

namespace agent
{
    namespace
    {
        // 三家客户端常驻，切换只是改一个枚举 + 换目标客户端
        LLMProviders::OpenAIClient &openai_client()
        {
            static LLMProviders::OpenAIClient c;
            return c;
        }
        LLMProviders::OllamaClient &ollama_client()
        {
            static LLMProviders::OllamaClient c;
            return c;
        }
        LLMProviders::LlamaClient &llama_client()
        {
            static LLMProviders::LlamaClient c;
            return c;
        }

        ProviderType &current_type()
        {
            static ProviderType t = ProviderType::OpenAI;
            return t;
        }

        // 从 <workspace>/tokens/providers/<id>.enc 读回解密后的 api key
        std::string load_api_key(const std::string &provider_id)
        {
            const fs::path key_file = config::tokens_dir() / "providers" / (provider_id + ".enc");
            std::error_code ec;
            if (!fs::exists(key_file, ec))
                return {};
            std::string encrypted = tool_unit::readFile(paths::to_utf8(key_file));
            if (!encrypted.empty() && encrypted.back() == '\n')
                encrypted.pop_back();
            if (encrypted.empty())
                return {};
            return crypto_unit::decrypt(encrypted, crypto_context::key());
        }
    } // namespace

    ProviderType current_provider_type() { return current_type(); }

    std::string provider_to_string()
    {
        switch (current_type())
        {
        case ProviderType::Ollama: return "ollama";
        case ProviderType::Llama: return "llama";
        case ProviderType::OpenAI: break;
        }
        return "openai";
    }

    std::string client_models()
    {
        switch (current_type())
        {
        case ProviderType::Ollama: return ollama_client().models();
        case ProviderType::Llama: return llama_client().models();
        case ProviderType::OpenAI: break;
        }
        return openai_client().models();
    }

    bool client_generate(json &req, json &resp)
    {
        switch (current_type())
        {
        case ProviderType::Ollama: return ollama_client().generate(req, resp);
        case ProviderType::Llama: return llama_client().generate(req, resp);
        case ProviderType::OpenAI: break;
        }
        return openai_client().generate(req, resp);
    }

    std::string client_stream_generate(json &req,
                                       std::function<void(const std::string &)> on_token,
                                       std::function<void(const std::string &)> on_thinking)
    {
        switch (current_type())
        {
        case ProviderType::Ollama:
            return ollama_client().stream_generate(req, on_token, on_thinking);
        case ProviderType::Llama:
            return llama_client().stream_generate(req, on_token, on_thinking);
        case ProviderType::OpenAI:
            break;
        }
        return openai_client().stream_generate(req, on_token, on_thinking);
    }

    void switch_provider(const std::string &provider_type, const std::string &base_url,
                         const std::string &api_key)
    {
        if (provider_type == "ollama")
        {
            current_type() = ProviderType::Ollama;
            ollama_client().set_base_url(base_url);
            ollama_client().set_api_key(api_key);
        }
        else if (provider_type == "llama")
        {
            current_type() = ProviderType::Llama;
            llama_client().set_base_url(base_url);
            llama_client().set_api_key(api_key);
        }
        else
        {
            current_type() = ProviderType::OpenAI;
            openai_client().set_base_url(base_url);
            openai_client().set_api_key(api_key);
        }
        logx::info("provider", "switched to " + provider_to_string() + " @ " + base_url);
    }

    json *find_provider(const std::string &provider_id)
    {
        if (!run_unit::settings.contains("providers"))
            return nullptr;
        for (auto &p : run_unit::settings["providers"])
            if (p.value("id", "") == provider_id)
                return &p;
        return nullptr;
    }

    bool switch_provider_by_id(const std::string &provider_id)
    {
        json *p = find_provider(provider_id);
        if (!p)
            return false;
        switch_provider(p->value("type", "openai"), p->value("server_address", ""),
                        load_api_key(provider_id));
        run_unit::settings["current_provider"] = provider_id;
        config::save();
        return true;
    }

    void init_from_settings()
    {
        std::string id = run_unit::settings.value("current_provider", "openai-default");

        // 兼容极老配置：没有 providers 数组时用 provider / server_address 迁移
        if (!run_unit::settings.contains("providers") || !run_unit::settings["providers"].is_array() ||
            run_unit::settings["providers"].empty())
        {
            json p = {
                {"id", "openai-default"},
                {"name", "OpenAI"},
                {"type", run_unit::settings.value("provider", "openai")},
                {"server_address", run_unit::settings.value("server_address", "http://localhost:11434")},
                {"has_key", false}};
            run_unit::settings["providers"] = json::array({p});
            run_unit::settings["current_provider"] = "openai-default";
            config::save();
            id = "openai-default";
        }

        if (!switch_provider_by_id(id))
        {
            const std::string first = run_unit::settings["providers"][0].value("id", "");
            if (!first.empty())
                switch_provider_by_id(first);
        }
    }

    std::string hex(const std::uint8_t *data, size_t len)
    {
        static const char *digits = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; ++i)
        {
            out += digits[(data[i] >> 4) & 0x0F];
            out += digits[data[i] & 0x0F];
        }
        return out;
    }
} // namespace agent
