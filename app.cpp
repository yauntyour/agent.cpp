#include <iostream>
#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <vector>
#include <cstdlib>
#include <mutex>
#include <format>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "servic.cpp/servic.hpp"
#include "servic.cpp/router/router.hpp"
#include "agent.hpp"
#include "servic.cpp/tiny_sha.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace app
{
    // 默认配置模板 —— settings.json 找不到时自动生成
    static const std::string DEFAULT_SETTINGS = R"({
    "agent_name": "assistant",
    "filesystem": {
        "auto_expand": false
    },
    "channels": [
        {
            "config": {
                "backend_url": "http://127.0.0.1:8080/api/input",
                "model": "default",
                "proxy": "http://127.0.0.1:10809",
                "think": false,
                "timeout": 600
            },
            "name": "Telegram",
            "path": "sys/tg_bot.py",
            "status": "active",
            "user_count": 1
        },
        {
            "config": {
                "backend_url": "http://127.0.0.1:8080/api/input",
                "ilink_base": "https://ilinkai.weixin.qq.com",
                "model": "default",
                "think": false,
                "timeout": 600
            },
            "name": "WeChat",
            "path": "sys/wx_bot.py",
            "status": "active",
            "user_count": 1
        }
    ],
    "current_provider": "openai-default",
    "max_context": 1048576,
    "max_mpc_rounds": 5,
    "model": "uGemma4",
    "webui_password": "a2aba198385559c15dc12398e197d556ef3cd6b45329d003b8886a0eecedec05",
    "prompt": "agent.txt",
    "providers": [
        {
            "has_key": false,
            "id": "openai-default",
            "name": "OpenAI",
            "server_address": "http://localhost:11434",
            "type": "openai"
        }
    ],
    "stream": true,
    "user_name": "Yauntyours",
    "workspace": "./workspace/"
})";

    void replaceAll(std::string &str, const std::string &from, const std::string &to)
    {
        if (from.empty())
            return;
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos)
        {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    }

    static std::string Admin;
    static std::string system_prompt;

    // 日志工具：将所有错误写入 webui.log
    static void webui_log(const std::string &level, const std::string &context, const std::string &message)
    {
        try
        {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::string time_str = std::asctime(std::localtime(&t));
            if (!time_str.empty() && time_str.back() == '\n')
                time_str.pop_back();

            std::string ws = run_unit::settings.value("workspace", ".");
            std::string log_path = ws + "/webui.log";
            std::ofstream log_file(log_path, std::ios::app);
            if (log_file.is_open())
            {
                log_file << "[" << time_str << "] [" << level << "] [" << context << "] " << message << std::endl;
            }
        }
        catch (...)
        {
            // 日志本身不抛出异常
        }
    }
    static std::string tools_list_str; // 工具列表的字符串表示

    // ==================== 模型供应商支持 ====================
    enum class ProviderType
    {
        OpenAI,
        Ollama,
        Llama
    };
    static ProviderType current_provider = ProviderType::OpenAI;
    static LLMProviders::OpenAIClient openai_client;
    static LLMProviders::OllamaClient ollama_client;
    static LLMProviders::LlamaClient llama_client;

    std::string client_models()
    {
        switch (current_provider)
        {
        case ProviderType::OpenAI:
            return openai_client.models();
        case ProviderType::Ollama:
            return ollama_client.models();
        case ProviderType::Llama:
            return llama_client.models();
        }
        return "";
    }

    bool client_generate(nlohmann::json &req, nlohmann::json &resp)
    {
        switch (current_provider)
        {
        case ProviderType::OpenAI:
            return openai_client.generate(req, resp);
        case ProviderType::Ollama:
            return ollama_client.generate(req, resp);
        case ProviderType::Llama:
            return llama_client.generate(req, resp);
        }
        return false;
    }

    std::string client_stream_generate(
        nlohmann::json &req,
        std::function<void(const std::string &)> on_token,
        std::function<void(const std::string &)> on_thinking = nullptr)
    {
        switch (current_provider)
        {
        case ProviderType::OpenAI:
            return openai_client.stream_generate(req, on_token, on_thinking);
        case ProviderType::Ollama:
        case ProviderType::Llama:
        {
            // Ollama/Llama 不支持流式，回退到非流式
            nlohmann::json resp;
            if (current_provider == ProviderType::Ollama)
                ollama_client.generate(req, resp);
            else
                llama_client.generate(req, resp);
            if (resp.contains("choices") && !resp["choices"].empty())
            {
                std::string content = resp["choices"][0]["message"]["content"].get<std::string>();
                if (on_token)
                    on_token(content);
                return content;
            }
            return "";
        }
        }
        return "";
    }

    void switch_provider(const std::string &provider_type, const std::string &base_url, const std::string &api_key)
    {
        if (provider_type == "ollama")
        {
            current_provider = ProviderType::Ollama;
            ollama_client.set_base_url(base_url);
            ollama_client.set_api_key(api_key);
            std::cout << "Provider switched to Ollama: " << base_url << std::endl;
        }
        else if (provider_type == "llama")
        {
            current_provider = ProviderType::Llama;
            llama_client.set_base_url(base_url);
            llama_client.set_api_key(api_key);
            std::cout << "Provider switched to Llama: " << base_url << std::endl;
        }
        else
        {
            current_provider = ProviderType::OpenAI;
            openai_client.set_base_url(base_url);
            openai_client.set_api_key(api_key);
            std::cout << "Provider switched to OpenAI: " << base_url << std::endl;
        }
    }

    // 根据 provider id 从 providers 数组中查找并切换
    bool switch_provider_by_id(const std::string &provider_id)
    {
        if (!run_unit::settings.contains("providers"))
            return false;
        auto &providers = run_unit::settings["providers"];
        for (auto &p : providers)
        {
            if (p.value("id", "") == provider_id)
            {
                std::string type = p.value("type", "openai");
                std::string base_url = p.value("server_address", "");

                // 从加密文件读取 api_key
                std::string api_key = "";
                std::string ws = run_unit::settings.value("workspace", ".");
                std::string key_file = ws + "/tokens/providers/" + provider_id + ".enc";
                if (std::filesystem::exists(key_file))
                {
                    std::string encrypted = tool_unit::readFile(key_file);
                    if (!encrypted.empty())
                    {
                        if (encrypted.back() == '\n')
                            encrypted.pop_back();
                        api_key = crypto_unit::decrypt(encrypted, crypto_context::key());
                    }
                }

                switch_provider(type, base_url, api_key);
                run_unit::settings["current_provider"] = provider_id;
                tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));
                return true;
            }
        }
        return false;
    }

    // 根据 provider id 从 providers 数组中查找
    json *find_provider(const std::string &provider_id)
    {
        if (!run_unit::settings.contains("providers"))
            return nullptr;
        auto &providers = run_unit::settings["providers"];
        for (auto &p : providers)
        {
            if (p.value("id", "") == provider_id)
            {
                return &p;
            }
        }
        return nullptr;
    }

    std::string provider_to_string()
    {
        switch (current_provider)
        {
        case ProviderType::OpenAI:
            return "openai";
        case ProviderType::Ollama:
            return "ollama";
        case ProviderType::Llama:
            return "llama";
        }
        return "openai";
    }

    std::string to_hex_string(const uint8_t *hash, size_t len)
    {
        std::stringstream ss;
        for (size_t i = 0; i < len; i++)
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        return ss.str();
    }

    int init_app(const std::string &setting_path = "settings.json", const std::string &password = "", const std::string &apikey = "")
    {
        // 当 settings.json 不存在时，自动使用默认配置模板生成
        if (!std::filesystem::exists(setting_path))
        {
            std::cout << "Warning - settings.json not found, creating default..." << std::endl;
            tool_unit::writeFile(setting_path, DEFAULT_SETTINGS);
        }
        run_unit::init_check(setting_path);

        // 初始化供应商（使用 providers 数组）
        {
            std::string current_prov_id = run_unit::settings.value("current_provider", "openai-default");
            // 兼容旧版配置：如果 providers 数组不存在，从旧的 provider 和 server_address 迁移
            if (!run_unit::settings.contains("providers") || run_unit::settings["providers"].empty())
            {
                std::string old_provider = run_unit::settings.value("provider", "openai");
                std::string old_server = run_unit::settings.value("server_address", "http://localhost:11434");
                json new_provider = {
                    {"id", "openai-default"},
                    {"name", "OpenAI"},
                    {"type", old_provider},
                    {"server_address", old_server},
                    {"has_key", false}};
                run_unit::settings["providers"] = json::array({new_provider});
                run_unit::settings["current_provider"] = "openai-default";
                tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));
                current_prov_id = "openai-default";
            }
            if (!switch_provider_by_id(current_prov_id))
            {
                // 如果找不到指定的 provider，使用第一个
                auto &providers = run_unit::settings["providers"];
                if (!providers.empty())
                {
                    std::string first_id = providers[0].value("id", "");
                    if (!first_id.empty())
                        switch_provider_by_id(first_id);
                }
            }
        }

        // 初始化 libsodium（必须在任何 crypto_* 函数前调用）
        if (sodium_init() < 0)
        {
            std::cerr << "Error: libsodium initialization failed" << std::endl;
            exit(1);
        }

        // ——— 启动密码校验：workspace/sys/key 存储 Argon2id 哈希，输入密码必须匹配 ———
        {
            std::string ws = run_unit::settings["workspace"].get<std::string>();
            std::string key_path = ws + "/sys/key";

            if (!std::filesystem::exists(key_path))
            {
                // 首次运行：生成 key 文件
                if (!password.empty())
                {
                    char stored_hash[crypto_pwhash_STRBYTES];
                    if (crypto_pwhash_str(stored_hash, password.c_str(), password.size(),
                                          crypto_pwhash_OPSLIMIT_MODERATE,
                                          crypto_pwhash_MEMLIMIT_MODERATE) != 0)
                    {
                        std::cerr << "Error: failed to hash password" << std::endl;
                        exit(1);
                    }
                    tool_unit::writeFile(key_path, std::string(stored_hash));
                    std::cout << "Password hash saved to workspace/sys/key" << std::endl;
                }
            }
            else
            {
                // 已有 key 文件：强制验证
                if (password.empty())
                {
                    std::cerr << "Error: password required but not provided" << std::endl;
                    exit(1);
                }
                std::string stored_hash_str = tool_unit::readFile(key_path);
                if (!stored_hash_str.empty() && stored_hash_str.back() == '\n')
                    stored_hash_str.pop_back();
                if (crypto_pwhash_str_verify(stored_hash_str.c_str(),
                                             password.c_str(), password.size()) != 0)
                {
                    std::cerr << "Error: incorrect password — system terminated" << std::endl;
                    exit(1);
                }
                std::cout << "Password verified OK" << std::endl;
            }
        }

        // 用密码派生 32 字节加密密钥，API key 以密文形式保存在内存中
        crypto_context::init_key_from_password(password);
        // 设置 API key 到当前供应商
        if (current_provider == ProviderType::OpenAI)
            openai_client.set_api_key(apikey);
        else if (current_provider == ProviderType::Ollama)
            ollama_client.set_api_key(apikey);
        else
            llama_client.set_api_key(apikey);

        // SHA3-256 密码哈希注入 settings（供 Web UI 会话认证）
        {
            uint8_t password_hash[SHA3_256_DIGEST_SIZE];
            if (!SHA3_256((const uint8_t *)password.c_str(), password.length(), password_hash))
                exit(1);
            run_unit::settings.emplace("webui_password", to_hex_string(password_hash, SHA3_256_DIGEST_SIZE));
        }

        Admin = run_unit::settings["user_name"].get<std::string>();

        system_prompt = tool_unit::readFile(
                            run_unit::settings["workspace"].get_ref<const std::string &>() +
                            run_unit::settings["prompt"].get_ref<const std::string &>()) +
                        run_unit::cs_prompt;
        replaceAll(system_prompt, "    ", "");
        replaceAll(system_prompt, "\r\n", "");
        return 0;
    }

    // 辅助：提取消息中的文本内容（用于统计长度等）
    static std::string extract_text(const json &msg)
    {
        if (msg["content"].is_string())
            return msg["content"].get<std::string>();
        if (msg["content"].is_array())
        {
            std::string text;
            for (auto &part : msg["content"])
                if (part["type"] == "text")
                    text += part["text"].get<std::string>();
            return text;
        }
        return "";
    }

    // ------------- 记忆管理（基于会话） -------------
    int save_memory(std::shared_ptr<run_unit::SessionContext> session_ptr, const std::string &model)
    {
        try
        {
            size_t total_prompt_tokens = 0;
            size_t total_completion_tokens = 0;
            std::string combined_query = session_ptr->summary_query();
            nlohmann::json response;
            nlohmann::json req = {
                {"model", model},
                {"messages", {{{"role", "system"}, {"content", combined_query}}}},
                {"stream", false}};
            client_generate(req, response);
            if (response.contains("usage"))
            {
                auto &usage = response["usage"];
                if (usage.contains("prompt_tokens"))
                    total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                if (usage.contains("completion_tokens"))
                    total_completion_tokens += usage["completion_tokens"].get<size_t>();
            }

            std::string content = response["choices"][0]["message"]["content"].get<std::string>();
            try
            {
                auto parsed = nlohmann::json::parse(content);
                if (parsed.contains("abstracts"))
                    session_ptr->memory["abstracts"] = parsed["abstracts"].get<std::string>();
                if (parsed.contains("keywords"))
                    session_ptr->memory["keywords"] = parsed["keywords"].get<std::string>();
            }
            catch (...)
            {
                session_ptr->memory["abstracts"] = content;
            }

            std::cout << "Memory saved successfully." << std::endl;

            session_ptr->memory["created_at"] = std::to_string(std::time(nullptr));
            session_ptr->last_saved_index = session_ptr->messages.size();

            auto &mem_usage = run_unit::agent_data_manager.data["usages"]["memory"];
            mem_usage["prompt_cost"] = mem_usage.value("prompt_cost", 0) + total_prompt_tokens;
            mem_usage["completion_cost"] = mem_usage.value("completion_cost", 0) + total_completion_tokens;
            mem_usage["total_cost"] = mem_usage.value("total_cost", 0) + total_prompt_tokens + total_completion_tokens;
        }
        catch (const std::exception &e)
        {
            std::cerr << "ERROR - Memory summarize: " << e.what() << '\n';
            webui_log("ERROR", "save_memory", e.what());
        }
        return 0;
    }
    namespace server
    {
        std::string build_http_response(int status_code, const std::string &content_type, const std::string &body, bool cors = true)
        {
            std::string final_content_type = "";
            // 自动为常见文本类型添加 charset，避免中文乱码
            if (final_content_type.find("application/json") != std::string::npos ||
                final_content_type.find("text/") != std::string::npos)
            {
                if (final_content_type.find("charset=") == std::string::npos)
                {
                    final_content_type += "; charset=utf-8";
                }
            }
            std::ostringstream oss;
            oss << "HTTP/1.1 " << status_code << " " << (status_code == 200 ? "OK" : "Not Found") << "\r\n";
            oss << "Content-Type: " << content_type + final_content_type << "\r\n";
            oss << "Content-Length: " << body.length() << "\r\n";
            if (cors)
            {
                oss << "Access-Control-Allow-Origin: *\r\n";
                oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
                oss << "Access-Control-Allow-Headers: Content-Type\r\n";
            }
            oss << "\r\n"
                << body;
            return oss.str();
        }

        int handle_root(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_input_packed(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_settings(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_data(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_provider(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        // ——— 供应商管理 ———
        int handle_providers_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_providers_add(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_providers_update(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_providers_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        // ——— 供应商 API Key 加密存储 ———
        int handle_provider_key_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_provider_key_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        // ——— 频道 Token 管理 ———
        int handle_channel_token_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_channel_token_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        // ——— 内置 Bot 状态 / 登录二维码 ———
        int handle_channel_qr(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_channel_status(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_channel_start(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_tools_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_todos_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_todos_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_todos_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_todos_new(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_new_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_session_clear(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_session_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_session_get_msg(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_session_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_session_memory(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_tools_toggle(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_fs_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_fs_used(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        void handle_input_streaming(std::string &input, rt::WriteCallback write, const std::map<std::string, std::string> &params);

        void register_routes(rt::router &router)
        {
            router.on("/", handle_root);
            router.on("/api/models", handle_models);
            router.on("/api/settings", handle_settings);
            router.on("/api/data", handle_data);
            router.on("/api/provider", handle_provider);

            // 供应商管理 API
            router.on("/api/providers", handle_providers_list);
            router.on("/api/providers/add", handle_providers_add);
            router.on("/api/providers/update", handle_providers_update);
            router.on("/api/providers/delete", handle_providers_delete);

            // 供应商 API Key 加密存储 API
            router.on("/api/provider/key", handle_provider_key_set);     // POST — 加密存储
            router.on("/api/provider/key/:id", handle_provider_key_get); // GET — 解密读取

            router.on("/api/input", handle_input_packed);
            router.on("/api/session", handle_session_list);
            router.on("/api/session/new", handle_new_session);
            router.on("/api/session/msg", handle_session_get_msg);
            router.on("/api/session/delete", handle_session_delete);
            router.on("/api/session/memory", handle_session_memory);
            router.on("/api/session/clear", handle_session_clear);

            router.on("/api/channels", handle_channels_list);
            router.on("/api/channel/token", handle_channel_token_set);       // POST — 加密存储
            router.on("/api/channel/token/:name", handle_channel_token_get); // GET — 解密读取
            router.on("/api/channel/qr/:name", handle_channel_qr);           // GET — 登录二维码/状态
            router.on("/api/channel/status", handle_channel_status);         // GET — 全部 Bot 状态
            router.on("/api/channel/start", handle_channel_start);           // POST — 运行时启动指定频道 Bot
            router.on("/api/tools", handle_tools_list);
            router.on("/api/tools/toggle", handle_tools_toggle);
            router.on("/api/fs/list", handle_fs_list);
            router.on("/api/fs/used", handle_fs_used);
            router.on("/api/todos", handle_todos_list);
            router.on("/api/todos/new", handle_todos_new);
            router.on("/api/todos/delete/:id", handle_todos_delete);
            router.on("/api/todos/:id", handle_todos_setting);

            // 流式路由
            router.on_stream("/api/input/stream", handle_input_streaming);
        }

        int handle_root(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::string html;
            try
            {
                html = tool_unit::readFile(run_unit::settings["workspace"].get_ref<const std::string &>() + "/webui.html");
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_root", e.what());
                html = "<h1>Welcome</h1><p>Error loading webui.html</p>";
            }
            catch (...)
            {
                webui_log("ERROR", "handle_root", "unknown error");
                html = "<h1>Welcome</h1><p>Error loading webui.html</p>";
            }
            output = build_http_response(200, "text/html", html);
            return rt::FLAG_DONE;
        }
        int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                output = build_http_response(200, "application/json", client_models());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_models", e.what());
                output = build_http_response(500, "application/json", R"({"error":"cannot connect to Ollama"})");
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_models", "unknown error");
                output = build_http_response(500, "application/json", R"({"error":"cannot connect to Ollama"})");
                return rt::FLAG_ERROR;
            }
        }
        int handle_session_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            json resp = {{"session_list", run_unit::agent_session_manager.list_sessions()}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        int handle_new_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            auto new_session = run_unit::agent_session_manager.create();
            json resp = {{"status", "OK"}, {"session_id", new_session->session_id}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        int handle_session_clear(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            run_unit::agent_session_manager.clear_current();
            json resp = {{"status", "cleared"}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        int handle_session_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string session_id = request["session_id"];

                // 检查是否为频道会话（系统频道不可删除）
                auto &channels = run_unit::settings["channels"];
                bool is_channel = false;
                for (auto &ch : channels)
                {
                    if (ch["name"].get<std::string>() == session_id)
                    {
                        is_channel = true;
                        break;
                    }
                }
                if (is_channel)
                {
                    output = build_http_response(403, "application/json",
                                                 R"({"error":"Cannot delete a channel session"})");
                    return rt::FLAG_ERROR;
                }

                // 从内存移除
                run_unit::agent_session_manager.remove_session(session_id);

                // 删除磁盘文件
                std::string ws = run_unit::settings["workspace"].get<std::string>();
                std::string session_file = ws + "/sessions/" + session_id + ".json";
                std::string memory_file = ws + "/memorys/" + session_id + ".json";
                std::string asset_file = ws + "/assets/messages/" + session_id + ".json";

                if (std::filesystem::exists(session_file))
                    std::filesystem::remove(session_file);
                if (std::filesystem::exists(memory_file))
                    std::filesystem::remove(memory_file);
                if (std::filesystem::exists(asset_file))
                    std::filesystem::remove(asset_file);

                output = build_http_response(200, "application/json", R"({"status":"deleted"})");
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_session_delete", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }
        int handle_session_get_msg(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string session_id = request["session_id"];

                auto session = run_unit::agent_session_manager.get(session_id);
                if (!session)
                {
                    output = build_http_response(404, "application/json", "{}");
                    return rt::FLAG_ERROR;
                }
                run_unit::agent_session_manager.change_session(session_id);
                bool has_memory = !session->is_memory_empty();
                json resp = {{"messages", session->messages}, {"memory", has_memory}};
                if (has_memory)
                    resp["memory_created_at"] = session->memory["created_at"];
                output = build_http_response(200, "application/json", resp.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_session_get_msg", e.what());
                output = build_http_response(500, "application/json", "{}");
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_session_get_msg", "unknown error");
                output = build_http_response(500, "application/json", "{}");
                return rt::FLAG_ERROR;
            }
        }
        int handle_session_memory(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                auto session = run_unit::agent_session_manager.get_current();
                save_memory(session, run_unit::settings["model"].get<std::string>());
                json resp = {{"status", "done"}};
                resp["memory_created_at"] = session->memory["created_at"];
                output = build_http_response(200, "application/json", resp.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_session_memory", e.what());
                output = build_http_response(500, "application/json", json{{"status", "failed"}}.dump());
                return rt::FLAG_ERROR;
            }
        }
        int handle_settings(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json input_data = json::parse(body);

                if (input_data["updata"].get<bool>() && input_data.contains("settings"))
                {
                    if (run_unit::validateJsonFormat(input_data["settings"]))
                    {
                        run_unit::settings = input_data["settings"];
                        tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));
                    }
                    else
                    {
                        output = build_http_response(500, "text/plain", "Settings saved failed.");
                        return rt::FLAG_ERROR;
                    }
                }
                output = build_http_response(200, "application/json", run_unit::settings.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_settings", e.what());
                output = build_http_response(500, "text/plain", "Settings saved failed.");
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_settings", "unknown error");
                output = build_http_response(500, "text/plain", "Settings saved failed.");
                return rt::FLAG_ERROR;
            }
        }
        int handle_data(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                output = build_http_response(200, "application/json", run_unit::agent_data_manager.data.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_data", e.what());
                output = build_http_response(500, "application/json", R"({"error":"Fail to get data"})");
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_data", "unknown error");
                output = build_http_response(500, "application/json", R"({"error":"Fail to get data"})");
                return rt::FLAG_ERROR;
            }
        }
        int handle_provider(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string provider_id = request.value("provider_id", "");
                if (provider_id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing provider_id"})");
                    return rt::FLAG_ERROR;
                }

                if (!switch_provider_by_id(provider_id))
                {
                    output = build_http_response(404, "application/json", R"({"error":"provider not found"})");
                    return rt::FLAG_ERROR;
                }

                json resp = {
                    {"status", "OK"},
                    {"provider_id", provider_id}};
                output = build_http_response(200, "application/json", resp.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_provider", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // —————————————— 供应商管理 API ——————————————

        // GET /api/providers → 返回所有供应商列表
        int handle_providers_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                json resp = {
                    {"providers", run_unit::settings.value("providers", json::array())},
                    {"current_provider", run_unit::settings.value("current_provider", "")}};
                output = build_http_response(200, "application/json", resp.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_list", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // POST /api/providers/add → 添加新供应商
        int handle_providers_add(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string id = request.value("id", "");
                std::string name = request.value("name", "");
                std::string type = request.value("type", "openai");
                std::string server_address = request.value("server_address", "");
                std::string api_key = request.value("api_key", "");

                if (id.empty() || name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id or name"})");
                    return rt::FLAG_ERROR;
                }

                // 检查 id 是否已存在
                if (!run_unit::settings.contains("providers"))
                    run_unit::settings["providers"] = json::array();
                for (auto &p : run_unit::settings["providers"])
                {
                    if (p.value("id", "") == id)
                    {
                        output = build_http_response(409, "application/json", R"({"error":"provider id already exists"})");
                        return rt::FLAG_ERROR;
                    }
                }

                // 如果提供了 api_key，加密存储到文件
                if (!api_key.empty())
                {
                    std::string ws = run_unit::settings["workspace"].get<std::string>();
                    std::string key_dir = ws + "/tokens/providers";
                    std::filesystem::create_directories(key_dir);
                    std::string file_path = key_dir + "/" + id + ".enc";
                    std::string encrypted = crypto_unit::encrypt(api_key, crypto_context::key());
                    if (encrypted.empty())
                    {
                        output = build_http_response(500, "application/json", R"({"error":"encryption failed"})");
                        return rt::FLAG_ERROR;
                    }
                    tool_unit::writeFile(file_path, encrypted);
                }

                // 不在 settings.json 中存储 api_key，只标记是否有 key
                json new_provider = {
                    {"id", id},
                    {"name", name},
                    {"type", type},
                    {"server_address", server_address},
                    {"has_key", !api_key.empty()}};
                run_unit::settings["providers"].push_back(new_provider);
                tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));

                json resp = {{"status", "OK"}, {"provider", new_provider}};
                output = build_http_response(200, "application/json", resp.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_add", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // POST /api/providers/update → 更新供应商配置
        int handle_providers_update(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string id = request.value("id", "");
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return rt::FLAG_ERROR;
                }

                json *provider = find_provider(id);
                if (!provider)
                {
                    output = build_http_response(404, "application/json", R"({"error":"provider not found"})");
                    return rt::FLAG_ERROR;
                }

                // 更新字段（只更新提供的字段）
                if (request.contains("name"))
                    (*provider)["name"] = request["name"];
                if (request.contains("type"))
                    (*provider)["type"] = request["type"];
                if (request.contains("server_address"))
                    (*provider)["server_address"] = request["server_address"];

                // 如果提供了 api_key，加密存储到文件
                if (request.contains("api_key"))
                {
                    std::string api_key = request["api_key"].get<std::string>();
                    std::string ws = run_unit::settings["workspace"].get<std::string>();
                    std::string key_dir = ws + "/tokens/providers";
                    std::filesystem::create_directories(key_dir);
                    std::string file_path = key_dir + "/" + id + ".enc";

                    if (api_key.empty())
                    {
                        // 空 api_key → 删除文件
                        if (std::filesystem::exists(file_path))
                            std::filesystem::remove(file_path);
                        (*provider)["has_key"] = false;
                    }
                    else
                    {
                        std::string encrypted = crypto_unit::encrypt(api_key, crypto_context::key());
                        if (encrypted.empty())
                        {
                            output = build_http_response(500, "application/json", R"({"error":"encryption failed"})");
                            return rt::FLAG_ERROR;
                        }
                        tool_unit::writeFile(file_path, encrypted);
                        (*provider)["has_key"] = true;
                    }
                }

                tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));

                // 如果更新的是当前活跃的供应商，重新切换
                std::string current_id = run_unit::settings.value("current_provider", "");
                if (current_id == id)
                {
                    switch_provider_by_id(id);
                }

                json resp = {{"status", "OK"}, {"provider", *provider}};
                output = build_http_response(200, "application/json", resp.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_update", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // POST /api/providers/delete → 删除供应商
        int handle_providers_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string id = request.value("id", "");
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return rt::FLAG_ERROR;
                }

                if (!run_unit::settings.contains("providers"))
                {
                    output = build_http_response(404, "application/json", R"({"error":"no providers"})");
                    return rt::FLAG_ERROR;
                }

                // 不允许删除最后一个供应商
                if (run_unit::settings["providers"].size() <= 1)
                {
                    output = build_http_response(400, "application/json", R"({"error":"cannot delete the last provider"})");
                    return rt::FLAG_ERROR;
                }

                auto &providers = run_unit::settings["providers"];
                for (auto it = providers.begin(); it != providers.end(); ++it)
                {
                    if (it->value("id", "") == id)
                    {
                        // 如果删除的是当前供应商，切换到第一个
                        std::string current_id = run_unit::settings.value("current_provider", "");
                        if (current_id == id)
                        {
                            std::string first_id = providers[0].value("id", "");
                            if (first_id != id)
                                switch_provider_by_id(first_id);
                            else if (providers.size() > 1)
                                switch_provider_by_id(providers[1].value("id", ""));
                        }
                        providers.erase(it);
                        tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));
                        json resp = {{"status", "OK"}};
                        output = build_http_response(200, "application/json", resp.dump());
                        return rt::FLAG_DONE;
                    }
                }

                output = build_http_response(404, "application/json", R"({"error":"provider not found"})");
                return rt::FLAG_ERROR;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_delete", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // —————————————— 供应商 API Key 加密存储 API ——————————————

        // POST /api/provider/key  body: {"id":"deepseek-1","api_key":"sk-xxxx"}
        // 将 api_key 加密后存入 workspace/tokens/providers/<id>.enc
        int handle_provider_key_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);

                std::string id = req.value("id", "");
                std::string api_key = req.value("api_key", "");
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return rt::FLAG_ERROR;
                }

                std::string ws = run_unit::settings["workspace"].get<std::string>();
                std::string key_dir = ws + "/tokens/providers";
                std::filesystem::create_directories(key_dir);
                std::string file_path = key_dir + "/" + id + ".enc";

                if (api_key.empty())
                {
                    // 空 api_key → 删除文件
                    if (std::filesystem::exists(file_path))
                        std::filesystem::remove(file_path);
                    output = build_http_response(200, "application/json",
                                                 json{{"status", "deleted"}, {"id", id}}.dump());
                }
                else
                {
                    // 加密存储
                    std::string encrypted = crypto_unit::encrypt(api_key, crypto_context::key());
                    if (encrypted.empty())
                    {
                        output = build_http_response(500, "application/json", R"({"error":"encryption failed"})");
                        return rt::FLAG_ERROR;
                    }
                    tool_unit::writeFile(file_path, encrypted);
                    output = build_http_response(200, "application/json",
                                                 json{{"status", "saved"}, {"id", id}}.dump());
                }
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_provider_key_set", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // GET /api/provider/key/:id  → 返回解密后的 {id, api_key}
        int handle_provider_key_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                auto it = params.find("id");
                std::string id = (it != params.end()) ? it->second : "";
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return rt::FLAG_ERROR;
                }

                std::string ws = run_unit::settings["workspace"].get<std::string>();
                std::string file_path = ws + "/tokens/providers/" + id + ".enc";

                if (!std::filesystem::exists(file_path))
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "api_key not found"}, {"id", id}}.dump());
                    return rt::FLAG_ERROR;
                }

                std::string encrypted = tool_unit::readFile(file_path);
                if (!encrypted.empty() && encrypted.back() == '\n')
                    encrypted.pop_back();

                std::string api_key = crypto_unit::decrypt(encrypted, crypto_context::key());
                if (api_key.empty())
                {
                    output = build_http_response(500, "application/json", R"({"error":"decryption failed"})");
                    return rt::FLAG_ERROR;
                }

                output = build_http_response(200, "application/json",
                                             json{{"id", id}, {"api_key", api_key}}.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_provider_key_get", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            output = build_http_response(200, "application/json", run_unit::settings["channels"].dump());
            return rt::FLAG_DONE;
        }

        // —————————————— 频道 Token 加密存储 API ——————————————

        // POST /api/channel/token  body: {"name":"Telegram","token":"1234:xxxx"}
        // 将 token 加密后存入 workspace/tokens/<name>.enc
        int handle_channel_token_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);

                std::string name = req.value("name", "");
                std::string token = req.value("token", "");
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return rt::FLAG_ERROR;
                }

                std::string ws = run_unit::settings["workspace"].get<std::string>();
                std::string token_dir = ws + "/tokens";
                std::filesystem::create_directories(token_dir);
                std::string file_path = token_dir + "/" + name + ".enc";

                if (token.empty())
                {
                    // 空 token → 删除文件
                    if (std::filesystem::exists(file_path))
                        std::filesystem::remove(file_path);
                    output = build_http_response(200, "application/json",
                                                 json{{"status", "deleted"}, {"name", name}}.dump());
                }
                else
                {
                    // 加密存储
                    std::string encrypted = crypto_unit::encrypt(token, crypto_context::key());
                    if (encrypted.empty())
                    {
                        output = build_http_response(500, "application/json", R"({"error":"encryption failed"})");
                        return rt::FLAG_ERROR;
                    }
                    tool_unit::writeFile(file_path, encrypted);
                    output = build_http_response(200, "application/json",
                                                 json{{"status", "saved"}, {"name", name}}.dump());
                }
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_token_set", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // GET /api/channel/token/:name  → 返回解密后的 {name, token}
        // Python 机器人启动时调用此接口获取 bot_token
        int handle_channel_token_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                auto it = params.find("name");
                std::string name = (it != params.end()) ? it->second : "";
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return rt::FLAG_ERROR;
                }

                std::string ws = run_unit::settings["workspace"].get<std::string>();
                std::string file_path = ws + "/tokens/" + name + ".enc";

                if (!std::filesystem::exists(file_path))
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "token not found"}, {"name", name}}.dump());
                    return rt::FLAG_ERROR;
                }

                std::string encrypted = tool_unit::readFile(file_path);
                if (!encrypted.empty() && encrypted.back() == '\n')
                    encrypted.pop_back();

                std::string token = crypto_unit::decrypt(encrypted, crypto_context::key());
                if (token.empty())
                {
                    output = build_http_response(500, "application/json", R"({"error":"decryption failed"})");
                    return rt::FLAG_ERROR;
                }

                output = build_http_response(200, "application/json",
                                             json{{"name", name}, {"token", token}}.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_token_get", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // GET /api/channel/qr/:name → {name, state, detail, qr_svg(base64), qr_url, running}
        int handle_channel_qr(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                auto it = params.find("name");
                std::string name = (it != params.end()) ? it->second : "";
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return rt::FLAG_ERROR;
                }
                output = build_http_response(200, "application/json", bot::channel_qr_json(name).dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_qr", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // GET /api/channel/status → 全部内置 Bot 运行状态
        int handle_channel_status(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                output = build_http_response(200, "application/json", bot::channel_status_json().dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_status", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }

        // POST /api/channel/start  body: {"name":"WeChat"}
        // 运行时按 settings.json channels 配置拉起指定频道的内置 Bot
        int handle_channel_start(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);
                std::string name = req.value("name", "");
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return rt::FLAG_ERROR;
                }
                bool started = bot::start_channel(name);
                json out = bot::channel_qr_json(name);
                out["started"] = started;
                output = build_http_response(200, "application/json", out.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_start", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }
        int handle_tools_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            output = build_http_response(200, "application/json", run_unit::tools_list.dump());
            return rt::FLAG_DONE;
        }
        int handle_tools_toggle(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string tool_name = request["name"];
                bool enabled = request.value("enabled", true);

                bool found_local = false;
                for (auto &tool : run_unit::tools_list)
                {
                    if (tool["name"] == tool_name)
                    {
                        tool["enabled"] = enabled;
                        found_local = true;
                        break;
                    }
                }

                if (found_local)
                {
                    std::string tools_path = run_unit::settings["workspace"].get<std::string>() + "/tools/tools.json";
                    tool_unit::writeFile(tools_path, run_unit::tools_list.dump(4));
                }
                else
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "tool not found: " + tool_name}}.dump());
                    return rt::FLAG_ERROR;
                }

                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"name", tool_name}, {"enabled", enabled}}.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_tools_toggle", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }
        int handle_fs_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string req_path = request.value("path", "");

                std::string ws = run_unit::settings["workspace"].get<std::string>();
                std::string ws_abs = std::filesystem::absolute(ws).string();
                std::string full_path = ws_abs;
                if (!req_path.empty() && req_path != "/")
                {
                    full_path = (std::filesystem::path(ws_abs) / req_path).string();
                }
                full_path = std::filesystem::absolute(full_path).string();

                // 安全检查：不超出工作目录
                if (full_path.find(ws_abs) != 0)
                {
                    output = build_http_response(403, "application/json", R"({"error":"Path outside workspace"})");
                    return rt::FLAG_ERROR;
                }

                if (!std::filesystem::exists(full_path))
                {
                    output = build_http_response(404, "application/json", R"({"error":"Path not found"})");
                    return rt::FLAG_ERROR;
                }

                json entries = json::array();
                if (std::filesystem::is_directory(full_path))
                {
                    for (const auto &entry : std::filesystem::directory_iterator(full_path))
                    {
                        auto ftime = std::filesystem::last_write_time(entry.path());
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                        std::time_t t = std::chrono::system_clock::to_time_t(sctp);
                        std::string time_str = std::asctime(std::localtime(&t));
                        if (!time_str.empty() && time_str.back() == '\n')
                            time_str.pop_back();

                        std::string rel_path = std::filesystem::relative(entry.path(), std::filesystem::path(ws_abs)).string();
                        std::string ext = entry.path().extension().string();

                        json entry_json = {
                            {"name", entry.path().filename().string()},
                            {"path", rel_path},
                            {"ext", ext},
                            {"is_dir", entry.is_directory()},
                            {"size", entry.is_regular_file() ? std::to_string(std::filesystem::file_size(entry.path())) : "0"},
                            {"modified", time_str}};
                        entries.push_back(entry_json);
                    }
                }

                output = build_http_response(200, "application/json",
                                             json{{"entries", entries}, {"cwd", req_path.empty() ? "/" : req_path}}.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_fs_list", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }
        int handle_fs_used(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                auto used = run_unit::get_used_files();
                json arr = json::array();
                for (auto &f : used)
                    arr.push_back(f);
                output = build_http_response(200, "application/json", json{{"files", arr}}.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_fs_used", e.what());
                output = build_http_response(200, "application/json", R"({"files":[]})");
                return rt::FLAG_DONE;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_fs_used", "unknown error");
                output = build_http_response(200, "application/json", R"({"files":[]})");
                return rt::FLAG_DONE;
            }
        }
        int handle_todos_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                json::array_t todos = json::parse(tool_unit::readFile(
                    run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/todos.json"));
                output = build_http_response(200, "application/json", json(todos).dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_todos_list", e.what());
                output = build_http_response(500, "application/json", "[]");
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_todos_list", "unknown error");
                output = build_http_response(500, "application/json", "[]");
                return rt::FLAG_ERROR;
            }
        }
        int handle_todos_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string id = params.count("id") ? params.at("id") : "unknown";
                json::array_t todos = json::parse(tool_unit::readFile(
                    run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/todos.json"));
                for (size_t i = 0; i < todos.size(); ++i)
                {
                    if (todos[i]["id"].get<std::string>() == id)
                    {
                        todos[i] = request;
                        tool_unit::writeFile(run_unit::settings["workspace"].get_ref<const std::string &>() +
                                                 "/sys/todos.json",
                                             json(todos).dump());
                        output = build_http_response(200, "application/json", "{}");
                        return rt::FLAG_DONE;
                    }
                }
                output = build_http_response(404, "application/json", "{}");
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_todos_setting", e.what());
                output = build_http_response(500, "application/json", "{}");
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_todos_setting", "unknown error");
                output = build_http_response(500, "application/json", "{}");
                return rt::FLAG_ERROR;
            }
            return 0;
        }
        int handle_todos_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::string id = params.count("id") ? params.at("id") : "unknown";
            json::array_t todos = json::parse(tool_unit::readFile(
                run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/todos.json"));
            todos.erase(std::remove_if(todos.begin(), todos.end(), [&](const json &t)
                                       { return t["id"].get<std::string>() == id; }),
                        todos.end());
            tool_unit::writeFile(run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/todos.json",
                                 json(todos).dump());
            output = build_http_response(200, "application/json", "{}");
            return rt::FLAG_DONE;
        }
        int handle_todos_new(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                json::array_t todos = json::parse(tool_unit::readFile(
                    run_unit::settings["workspace"].get_ref<const std::string &>() + "/sys/todos.json"));
                todos.push_back(request);
                tool_unit::writeFile(run_unit::settings["workspace"].get_ref<const std::string &>() +
                                         "/sys/todos.json",
                                     json(todos).dump());
                output = build_http_response(200, "application/json", request.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_todos_new", e.what());
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_todos_new", "unknown error");
                return rt::FLAG_ERROR;
            }
        }
        void handle_input_streaming(std::string &input, rt::WriteCallback write, const std::map<std::string, std::string> &params)
        {
            auto sse = [&](const json &data)
            {
                write("data: " + data.dump() + "\n\n");
            };

            std::string model;
            std::string channel;
            bool think_mode = false;
            size_t total_prompt_tokens = 0;
            size_t total_completion_tokens = 0;
            std::string sid = "";
            std::shared_ptr<run_unit::SessionContext> session_ptr;

            try
            {
                size_t header_end = input.find("\r\n\r\n");
                if (header_end == std::string::npos)
                {
                    write("HTTP/1.1 400 Bad Request\r\n\r\n");
                    return;
                }
                std::string body = input.substr(header_end + 4);
                json request = json::parse(body);

                std::string user_message = request["messages"].get<std::string>();
                model = request.value("model", "default");
                if (model == "default")
                    model = run_unit::settings["model"].get<std::string>();

                // 确定会话
                if (request.contains("channel"))
                {
                    channel = request["channel"].get<std::string>();
                    session_ptr = run_unit::agent_session_manager.get(channel);
                    if (!session_ptr)
                    {
                        session_ptr = run_unit::agent_session_manager.create();
                        run_unit::agent_session_manager.change_session(session_ptr->session_id);
                    }
                }
                else if (request.contains("session_id"))
                {
                    sid = request["session_id"].get<std::string>();
                    session_ptr = run_unit::agent_session_manager.get(sid);
                    if (!session_ptr)
                    {
                        write("HTTP/1.1 400 Bad Request\r\n\r\n");
                        return;
                    }
                    run_unit::agent_session_manager.change_session(sid);
                }
                else
                {
                    session_ptr = run_unit::agent_session_manager.get_current();
                }

                // 发送 SSE HTTP 头
                std::string http_headers =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/event-stream; charset=utf-8\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Connection: keep-alive\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                    "Access-Control-Allow-Headers: Content-Type\r\n"
                    "\r\n";
                write(http_headers);

                // 构建上下文
                json context = json::array();
                context.push_back({{"role", "system"}, {"content", system_prompt}});
                context.push_back({{"role", "system"}, {"content", tools_list_str}});

                if (!session_ptr->is_memory_empty())
                    context.push_back({{"role", "system"}, {"content", session_ptr->memory["abstracts"]}});
                else
                    for (auto &msg : session_ptr->messages)
                        context.push_back(msg);

                json contents = json::array();
                if (!channel.empty())
                    contents.push_back({{"type", "text"}, {"text", "Messages received from " + channel + ":" + user_message}});
                else
                    contents.push_back({{"type", "text"}, {"text", user_message}});

                if (request.contains("images") && request["images"].is_array())
                {
                    for (auto &img : request["images"])
                        contents.push_back({{"type", "image_url"}, {"image_url", {{"url", img.get<std::string>()}}}});
                }
                json user_msg = {{"role", Admin}, {"content", contents}};
                context.push_back(user_msg);
                session_ptr->messages.push_back(user_msg);

                think_mode = request.value("think", false);

                // 工具/CS 循环（带流式输出）
                json thinkings = json::array();
                size_t max_rounds = run_unit::settings["max_mpc_rounds"].get<size_t>();
                size_t mpc_count = 0;
                json all_new_messages = json::array();

                for (; mpc_count < max_rounds; ++mpc_count)
                {
                    std::string thinking_text;
                    json llm_req = {
                        {"model", model},
                        {"messages", context},
                        {"think", think_mode}};

                    std::string response_text = client_stream_generate(
                        llm_req,
                        [&](const std::string &chunk)
                        {
                            sse({{"type", "token"}, {"content", chunk}, {"round", mpc_count}});
                        },
                        [&](const std::string &chunk)
                        {
                            thinking_text += chunk;
                            sse({{"type", "thinking"}, {"content", chunk}});
                        });

                    if (think_mode && !thinking_text.empty())
                        thinkings.push_back(thinking_text);

                    total_prompt_tokens += llm_req["messages"].dump().size() / 4;
                    total_completion_tokens += response_text.size() / 4;

                    json agent_response = {
                        {"role", run_unit::settings["agent_name"]},
                        {"content", response_text}};
                    context.push_back(agent_response);
                    session_ptr->messages.push_back(agent_response);

                    // 扫描工具/CS 调用
                    std::string sys_out;
                    auto [tools_called, tools_ok] = tool_unit::tools_scan(response_text, sys_out);
                    auto cs_called = cs_unit::cs_scan(response_text, sys_out);

                    if (tools_called == 0 && cs_called == 0)
                    {
                        break;
                    }

                    // 发送工具调用事件（每个工具独立发送 start/output/end）
                    auto tool_tags = extractAllTags(response_text, "tool");
                    auto sys_out_blocks = extractAllTags(sys_out, "system_output");

                    for (size_t ti = 0; ti < tool_tags.size(); ti++)
                    {
                        auto [name, args] = parseArgs(tool_tags[ti]);
                        sse({{"type", "tool_start"}, {"name", std::string(name)}, {"args", std::string(args)}, {"round", mpc_count}});

                        if (ti < sys_out_blocks.size())
                        {
                            sse({{"type", "tool_output"}, {"content", std::string(sys_out_blocks[ti])}, {"round", mpc_count}});
                        }

                        sse({{"type", "tool_end"}, {"name", std::string(name)}, {"round", mpc_count}});
                    }

                    // 添加工具消息到上下文
                    json sys_contents = json::array();
                    sys_contents.push_back({{"type", "text"}, {"text", sys_out}});
                    if (!tool_unit::image_queue.empty())
                    {
                        for (auto &img : tool_unit::image_queue)
                            sys_contents.push_back({{"type", "image_url"}, {"image_url", {{"url", img}}}});
                        tool_unit::image_queue.clear();
                    }
                    json sys_msg = {{"role", "tool"}, {"content", sys_contents}};
                    context.push_back(sys_msg);
                    session_ptr->messages.push_back(sys_msg);
                }

                // 自动触发记忆保存（上下文超过 max_context 时）
                {
                    size_t ctx_size = session_ptr->messages.dump().size();
                    size_t max_ctx = run_unit::settings["max_context"].get<size_t>();
                    if (ctx_size > max_ctx)
                    {
                        std::cout << "Auto-trigger memory save (context: " << ctx_size << " > max: " << max_ctx << ")" << std::endl;
                        save_memory(session_ptr, model);
                    }
                }

                // 更新使用统计
                if (run_unit::agent_data_manager.data["usages"].contains(sid))
                {
                    run_unit::agent_data_manager.data["usages"][sid]["prompt_cost"] = run_unit::agent_data_manager.data["usages"][sid]["prompt_cost"].get<size_t>() + total_prompt_tokens;
                    run_unit::agent_data_manager.data["usages"][sid]["completion_cost"] = run_unit::agent_data_manager.data["usages"][sid]["completion_cost"].get<size_t>() + total_completion_tokens;
                    run_unit::agent_data_manager.data["usages"][sid]["total_cost"] = run_unit::agent_data_manager.data["usages"][sid]["total_cost"].get<size_t>() + total_prompt_tokens + total_completion_tokens;
                }
                else
                {
                    run_unit::agent_data_manager.data["usages"][sid] = {
                        {"prompt_cost", total_prompt_tokens},
                        {"completion_cost", total_completion_tokens},
                        {"total_cost", total_prompt_tokens + total_completion_tokens}};
                }

                // 发送完成事件
                json usage = {
                    {"prompt_cost", total_prompt_tokens},
                    {"completion_cost", total_completion_tokens},
                    {"total_cost", total_prompt_tokens + total_completion_tokens}};
                sse({{"type", "done"}, {"usage", usage}});
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error in handle_input_streaming: " << e.what() << std::endl;
                webui_log("ERROR", "handle_input_streaming", e.what());
                try
                {
                    sse({{"type", "error"}, {"message", e.what()}});
                }
                catch (...)
                {
                }
            }
        }
        int handle_input_packed(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                if (header_end == std::string::npos)
                {
                    output = build_http_response(400, "application/json", R"({"error":"bad request"})");
                    return rt::FLAG_DONE;
                }
                std::string body = input.substr(header_end + 4);
                json request = json::parse(body);

                std::string user_message = request["messages"].get<std::string>();
                std::string model = request.value("model", "default");
                if (model == "default")
                    model = run_unit::settings["model"].get<std::string>();

                std::string channel;
                bool think_mode = request.value("think", false);
                size_t total_prompt_tokens = 0;
                size_t total_completion_tokens = 0;
                std::string sid = "";
                std::shared_ptr<run_unit::SessionContext> session_ptr;

                // 确定会话
                if (request.contains("channel"))
                {
                    channel = request["channel"].get<std::string>();
                    session_ptr = run_unit::agent_session_manager.get(channel);
                    if (!session_ptr)
                    {
                        session_ptr = run_unit::agent_session_manager.create();
                        run_unit::agent_session_manager.change_session(session_ptr->session_id);
                    }
                }
                else if (request.contains("session_id"))
                {
                    sid = request["session_id"].get<std::string>();
                    session_ptr = run_unit::agent_session_manager.get(sid);
                    if (!session_ptr)
                    {
                        output = build_http_response(400, "application/json", R"({"error":"session not found"})");
                        return rt::FLAG_DONE;
                    }
                    run_unit::agent_session_manager.change_session(sid);
                }
                else
                {
                    session_ptr = run_unit::agent_session_manager.get_current();
                }

                // 构建上下文
                json context = json::array();
                context.push_back({{"role", "system"}, {"content", system_prompt}});
                context.push_back({{"role", "system"}, {"content", tools_list_str}});

                if (!session_ptr->is_memory_empty())
                    context.push_back({{"role", "system"}, {"content", session_ptr->memory["abstracts"]}});
                else
                    for (auto &msg : session_ptr->messages)
                        context.push_back(msg);

                json contents = json::array();
                if (!channel.empty())
                    contents.push_back({{"type", "text"}, {"text", "Messages received from " + channel + ":" + user_message}});
                else
                    contents.push_back({{"type", "text"}, {"text", user_message}});

                if (request.contains("images") && request["images"].is_array())
                {
                    for (auto &img : request["images"])
                        contents.push_back({{"type", "image_url"}, {"image_url", {{"url", img.get<std::string>()}}}});
                }
                json user_msg = {{"role", Admin}, {"content", contents}};
                context.push_back(user_msg);
                session_ptr->messages.push_back(user_msg);

                // 工具/CS 循环（非流式）
                json thinkings = json::array();
                json tools_called_arr = json::array();
                std::string final_response;
                size_t max_rounds = run_unit::settings["max_mpc_rounds"].get<size_t>();
                size_t mpc_count = 0;

                for (; mpc_count < max_rounds; ++mpc_count)
                {
                    json llm_req = {
                        {"model", model},
                        {"messages", context},
                        {"think", think_mode}};

                    json response;
                    if (!client_generate(llm_req, response))
                    {
                        output = build_http_response(500, "application/json", R"({"error":"LLM generate failed"})");
                        return rt::FLAG_ERROR;
                    }

                    // 提取 thinking 内容
                    if (think_mode && response["choices"][0]["message"].contains("reasoning_content"))
                    {
                        thinkings.push_back(response["choices"][0]["message"]["reasoning_content"].get<std::string>());
                    }

                    // 提取回复文本
                    std::string response_text = response["choices"][0]["message"]["content"].get<std::string>();

                    json agent_response = {
                        {"role", run_unit::settings["agent_name"]},
                        {"content", response_text}};
                    context.push_back(agent_response);
                    session_ptr->messages.push_back(agent_response);

                    final_response += response_text;

                    // 记录 token 用量
                    if (response.contains("usage"))
                    {
                        auto &usage = response["usage"];
                        if (usage.contains("prompt_tokens"))
                            total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                        if (usage.contains("completion_tokens"))
                            total_completion_tokens += usage["completion_tokens"].get<size_t>();
                    }

                    // 扫描工具/CS 调用
                    std::string sys_out;
                    auto [tools_called, tools_ok] = tool_unit::tools_scan(response_text, sys_out);
                    auto cs_called = cs_unit::cs_scan(response_text, sys_out);

                    // 收集工具调用信息
                    auto tool_tags = extractAllTags(response_text, "tool");
                    auto sys_out_blocks = extractAllTags(sys_out, "system_output");

                    for (size_t ti = 0; ti < tool_tags.size(); ti++)
                    {
                        auto [name, args] = parseArgs(tool_tags[ti]);
                        json tool_entry = {
                            {"name", std::string(name)},
                            {"args", std::string(args)},
                            {"round", mpc_count}};
                        if (ti < sys_out_blocks.size())
                            tool_entry["output"] = std::string(sys_out_blocks[ti]);
                        tools_called_arr.push_back(tool_entry);
                    }

                    // 添加工具消息到上下文
                    json sys_contents = json::array();
                    sys_contents.push_back({{"type", "text"}, {"text", sys_out}});
                    if (!tool_unit::image_queue.empty())
                    {
                        for (auto &img : tool_unit::image_queue)
                            sys_contents.push_back({{"type", "image_url"}, {"image_url", {{"url", img}}}});
                        tool_unit::image_queue.clear();
                    }
                    json sys_msg = {{"role", "tool"}, {"content", sys_contents}};
                    context.push_back(sys_msg);
                    session_ptr->messages.push_back(sys_msg);
                }

                // 自动触发记忆保存（上下文超过 max_context 时）
                {
                    size_t ctx_size = session_ptr->messages.dump().size();
                    size_t max_ctx = run_unit::settings["max_context"].get<size_t>();
                    if (ctx_size > max_ctx)
                    {
                        std::cout << "Auto-trigger memory save (context: " << ctx_size << " > max: " << max_ctx << ")" << std::endl;
                        save_memory(session_ptr, model);
                    }
                }

                // 更新使用统计
                if (!sid.empty() && run_unit::agent_data_manager.data["usages"].contains(sid))
                {
                    run_unit::agent_data_manager.data["usages"][sid]["prompt_cost"] = run_unit::agent_data_manager.data["usages"][sid]["prompt_cost"].get<size_t>() + total_prompt_tokens;
                    run_unit::agent_data_manager.data["usages"][sid]["completion_cost"] = run_unit::agent_data_manager.data["usages"][sid]["completion_cost"].get<size_t>() + total_completion_tokens;
                    run_unit::agent_data_manager.data["usages"][sid]["total_cost"] = run_unit::agent_data_manager.data["usages"][sid]["total_cost"].get<size_t>() + total_prompt_tokens + total_completion_tokens;
                }
                else if (!sid.empty())
                {
                    run_unit::agent_data_manager.data["usages"][sid] = {
                        {"prompt_cost", total_prompt_tokens},
                        {"completion_cost", total_completion_tokens},
                        {"total_cost", total_prompt_tokens + total_completion_tokens}};
                }

                // 构建统一 JSON 响应
                json result = {
                    {"content", final_response},
                    {"thinking", thinkings},
                    {"tools", tools_called_arr},
                    {"rounds", mpc_count},
                    {"usage", {{"prompt_cost", total_prompt_tokens}, {"completion_cost", total_completion_tokens}, {"total_cost", total_prompt_tokens + total_completion_tokens}}}};

                output = build_http_response(200, "application/json", result.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error in handle_input_packed: " << e.what() << std::endl;
                webui_log("ERROR", "handle_input_packed", e.what());
                json err = {{"error", e.what()}};
                output = build_http_response(500, "application/json", err.dump());
                return rt::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_input_packed", "unknown error");
                output = build_http_response(500, "application/json", R"({"error":"unknown"})");
                return rt::FLAG_ERROR;
            }
        }
    } // namespace server
} // namespace app

int main(int argc, char *argv[])
{
    try
    {
        std::cout << get_system_status() << std::endl;

        // ——— 所有运行参数仅从 CLI 传入，不从 settings.json 读取 ———
        std::string settings_path = "settings.json";
        int port = 8080;
        std::string __pw = "";
        std::string apikey = "";

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--settings")
            {
                if (i + 1 < argc)
                    settings_path = argv[++i];
                else
                    throw std::runtime_error("Missing value after " + arg);
            }
            else if (arg == "-p" || arg == "--port")
            {
                if (i + 1 < argc)
                    port = std::stoi(argv[++i]);
                else
                    throw std::runtime_error("Missing value after " + arg);
            }
            else if (arg == "--password")
            {
                if (i + 1 < argc)
                    __pw = argv[++i];
                else
                    throw std::runtime_error("Missing value after " + arg);
            }
            else if (arg == "--apikey")
            {
                if (i + 1 < argc)
                    apikey = argv[++i];
                else
                    throw std::runtime_error("Missing value after " + arg);
            }
        }

        app::init_app(settings_path, __pw, apikey);
        boost::asio::io_context io_context;
        rt::router router;
        app::server::register_routes(router);

        // 启动内置 Telegram / WeChat 机器人（按 settings.json channels 配置）
        bot::start_channels();

        std::cout << "http://localhost:" << port << std::endl;
        servic::Server server(io_context, port);
        server.run(router);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}