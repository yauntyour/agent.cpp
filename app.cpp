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
    static size_t im_token_len = sizeof("<|im_start|>\n<|im_end|>") - 1;
    static std::string tools_list_str; // 工具列表的字符串表示
    static auto client = LLMProviders::OpenAIClient();

    std::string to_hex_string(const uint8_t *hash, size_t len)
    {
        std::stringstream ss;
        for (size_t i = 0; i < len; i++)
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        return ss.str();
    }

    int init_app(const std::string &setting_path = "settings.json", const std::string &password = "", const std::string &apikey = "")
    {
        run_unit::init_check(setting_path);
        client.set_base_url(run_unit::settings["server_address"].get_ref<const std::string &>());
        client.set_api_key(apikey);

        uint8_t password_hash[SHA3_256_DIGEST_SIZE];
        if (!SHA3_256((const uint8_t *)password.c_str(), password.length(), password_hash))
            return 1;
        run_unit::settings.emplace("password", to_hex_string(password_hash, SHA3_256_DIGEST_SIZE));

        Admin = run_unit::settings["user_name"].get<std::string>();
        // 仅启用 enabled 的工具
        {
            nlohmann::json enabled_tools = nlohmann::json::array();
            for (auto &t : run_unit::tools_list)
            {
                if (t.value("enabled", true))
                    enabled_tools.push_back(t);
            }
            tools_list_str = enabled_tools.dump();
        }

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
            auto [aq, kq] = session_ptr->summary_query();
            nlohmann::json response;
            nlohmann::json req = {
                {"model", model},
                {"messages", {{{"role", "system"}, {"content", aq}}}},
                {"stream", false}};
            client.generate(req, response);
            if (response.contains("usage"))
            {
                auto &usage = response["usage"];
                if (usage.contains("prompt_tokens"))
                    total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                if (usage.contains("completion_tokens"))
                    total_completion_tokens += usage["completion_tokens"].get<size_t>();
            }
            session_ptr->memory["abstracts"] = response["choices"][0]["message"]["content"].get<std::string>();
            std::cout << "Abstracts generated successfully." << std::endl;

            req["messages"][0]["content"] = kq;
            client.generate(req, response);
            if (response.contains("usage"))
            {
                auto &usage = response["usage"];
                if (usage.contains("prompt_tokens"))
                    total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                if (usage.contains("completion_tokens"))
                    total_completion_tokens += usage["completion_tokens"].get<size_t>();
            }
            session_ptr->memory["keywords"] = response["choices"][0]["message"]["content"].get<std::string>();
            std::cout << "Keywords generated successfully." << std::endl;

            run_unit::agent_data_manager.data["usages"]["memory"]["prompt_cost"] = run_unit::agent_data_manager.data["usages"]["memory"]["prompt_cost"].get<size_t>() + total_prompt_tokens;
            run_unit::agent_data_manager.data["usages"]["memory"]["completion_cost"] = run_unit::agent_data_manager.data["usages"]["memory"]["completion_cost"].get<size_t>() + total_completion_tokens;
            run_unit::agent_data_manager.data["usages"]["memory"]["total_cost"] = run_unit::agent_data_manager.data["usages"]["memory"]["total_cost"].get<size_t>() + total_prompt_tokens + total_completion_tokens;
        }
        catch (const std::exception &e)
        {
            std::cerr << "ERROR - Memory summarize: " << e.what() << '\n';
            webui_log("ERROR", "save_memory", e.what());
        }
        return 0;
    }

    int evolved_memory(std::shared_ptr<run_unit::SessionContext> session_ptr, const std::string &model)
    {
        try
        {
            size_t total_prompt_tokens = 0;
            size_t total_completion_tokens = 0;
            nlohmann::json response;
            nlohmann::json req = {
                {"model", model},
                {"messages", {{{"role", "memory"}, {"content", "Keywords:" + session_ptr->memory["keywords"].get<std::string>()}}, {{"role", "memory"}, {"content", session_ptr->memory["abstracts"]}}, {{"role", "system"}, {"content", "Evaluate the value of the memory content and chat history in improving the work of the user of this memory. "
                                                                                                                                                                                                                                         "If improvements are needed, output only the improved memory output; if no improvements are needed, output only [PASS]."}}}},
                {"stream", false}};

            // 插入所有会话消息（但跳过 system/memory？实际可以插入全部，这里略作简化）
            for (auto &msg : session_ptr->messages)
            {
                req["messages"].push_back(msg);
            }

            client.generate(req, response);
            if (response.contains("usage"))
            {
                auto &usage = response["usage"];
                if (usage.contains("prompt_tokens"))
                    total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                if (usage.contains("completion_tokens"))
                    total_completion_tokens += usage["completion_tokens"].get<size_t>();
            }
            auto output = response["choices"][0]["message"]["content"].get<std::string>();
            if (output.find("[PASS]") == std::string::npos)
            {
                session_ptr->memory["abstracts"] = std::move(output);
                std::cout << "Abstracts updated successfully." << std::endl;

                req["messages"][2]["content"] = "Please evaluate the relevance of the following content to the memory content, refine the keywords for the memory, and output only the refined keywords for the memory content.";
                client.generate(req, response);
                if (response.contains("usage"))
                {
                    auto &usage = response["usage"];
                    if (usage.contains("prompt_tokens"))
                        total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                    if (usage.contains("completion_tokens"))
                        total_completion_tokens += usage["completion_tokens"].get<size_t>();
                }
                session_ptr->memory["keywords"] = response["choices"][0]["message"]["content"].get<std::string>();
                std::cout << "Keywords updated successfully." << std::endl;
            }
            else
            {
                std::cout << "Memory not need to update." << std::endl;
            }
            run_unit::agent_data_manager.data["usages"]["memory"]["prompt_cost"] = run_unit::agent_data_manager.data["usages"]["memory"]["prompt_cost"].get<size_t>() + total_prompt_tokens;
            run_unit::agent_data_manager.data["usages"]["memory"]["completion_cost"] = run_unit::agent_data_manager.data["usages"]["memory"]["completion_cost"].get<size_t>() + total_completion_tokens;
            run_unit::agent_data_manager.data["usages"]["memory"]["total_cost"] = run_unit::agent_data_manager.data["usages"]["memory"]["total_cost"].get<size_t>() + total_prompt_tokens + total_completion_tokens;
        }
        catch (const std::exception &e)
        {
            std::cerr << "ERROR - Memory assessment: " << e.what() << '\n';
            webui_log("ERROR", "evolved_memory", e.what());
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
        int handle_input_stream(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_settings(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_data(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
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

        void handle_input_stream_streaming(std::string &input, rt::WriteCallback write, const std::map<std::string, std::string> &params);

        void register_routes(rt::router &router)
        {
            router.on("/", handle_root);
            router.on("/api/models", handle_models);
            router.on("/api/settings", handle_settings);
            router.on("/api/data", handle_data);

            router.on("/api/input", handle_input_stream);
            router.on("/api/session", handle_session_list);
            router.on("/api/session/new", handle_new_session);
            router.on("/api/session/msg", handle_session_get_msg);
            router.on("/api/session/delete", handle_session_delete);
            router.on("/api/session/memory", handle_session_memory);
            router.on("/api/session/clear", handle_session_clear);

            router.on("/api/channels", handle_channels_list);
            router.on("/api/tools", handle_tools_list);
            router.on("/api/tools/toggle", handle_tools_toggle);
            router.on("/api/fs/list", handle_fs_list);
            router.on("/api/fs/used", handle_fs_used);
            router.on("/api/todos", handle_todos_list);
            router.on("/api/todos/new", handle_todos_new);
            router.on("/api/todos/delete/:id", handle_todos_delete);
            router.on("/api/todos/:id", handle_todos_setting);

            // 流式路由
            router.on_stream("/api/input/stream", handle_input_stream_streaming);
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
                output = build_http_response(200, "application/json", client.models());
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
                json resp = {{"messages", session->messages}};
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
                if (session->is_memory_empty())
                    save_memory(session, run_unit::settings["model"].get<std::string>());
                else
                    evolved_memory(session, run_unit::settings["model"].get<std::string>());
                json resp = {{"status", "done"}};
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
        int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            output = build_http_response(200, "application/json", run_unit::settings["channels"].dump());
            return rt::FLAG_DONE;
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

                for (auto &tool : run_unit::tools_list)
                {
                    if (tool["name"] == tool_name)
                    {
                        tool["enabled"] = enabled;
                        break;
                    }
                }

                std::string tools_path = run_unit::settings["workspace"].get<std::string>() + "/tools/tools.json";
                tool_unit::writeFile(tools_path, run_unit::tools_list.dump(4));

                // 重建工具列表字符串
                nlohmann::json enabled_tools = nlohmann::json::array();
                for (auto &t : run_unit::tools_list)
                {
                    if (t.value("enabled", true))
                        enabled_tools.push_back(t);
                }
                tools_list_str = enabled_tools.dump();

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
        int handle_input_stream(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            json req_payload;
            std::string model;
            std::string channel;
            bool think_mode = false;
            size_t total_prompt_tokens = 0;
            size_t total_completion_tokens = 0;
            std::string sid = "";
            std::shared_ptr<run_unit::SessionContext> session_ptr;

            try
            {
                // 解析请求体
                size_t header_end = input.find("\r\n\r\n");
                if (header_end == std::string::npos)
                {
                    output = build_http_response(400, "text/plain", "Bad Request");
                    return rt::FLAG_ERROR;
                }
                std::string body = input.substr(header_end + 4);
                json request = json::parse(body);

                std::string user_message = request["messages"].get<std::string>();
                model = request.value("model", "default");
                if (model == "default")
                    model = run_unit::settings["model"].get<std::string>();

                // 1. 确定会话：频道消息 vs 普通消息
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
                        return rt::FLAG_ERROR;
                    }
                    run_unit::agent_session_manager.change_session(sid);
                }
                else
                {
                    session_ptr = run_unit::agent_session_manager.get_current();
                }

                // 2. 构建上下文数组（系统提示 + 工具列表 + 记忆 + 历史消息）
                json context = json::array();

                // 系统提示
                context.push_back({{"role", "system"}, {"content", system_prompt}});
                context.push_back({{"role", "system"}, {"content", tools_list_str}});

                // 记忆（如果有）
                if (!session_ptr->is_memory_empty())
                {
                    context.push_back({{"role", "memory"}, {"content", session_ptr->memory["abstracts"]}});
                }

                // 添加会话历史（加载自消息文件）
                for (auto &msg : session_ptr->messages)
                {
                    context.push_back(msg);
                }

                // 3. 添加当前用户消息
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

                // 思考模式
                think_mode = request.value("think", false);

                // 准备请求
                json llm_req = {
                    {"model", model},
                    {"messages", context},
                    {"stream", false},
                    {"think", think_mode}};

                // 第一次调用
                json response;
                if (!client.generate(llm_req, response))
                {
                    output = build_http_response(500, "application/json", R"({"error":"LLM call failed"})");
                    return rt::FLAG_ERROR;
                }

                if (response.contains("usage"))
                {
                    auto &usage = response["usage"];
                    if (usage.contains("prompt_tokens"))
                        total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                    if (usage.contains("completion_tokens"))
                        total_completion_tokens += usage["completion_tokens"].get<size_t>();
                }

                auto choices = response["choices"][0];
                std::string assistant_reply = "\r\n" + choices["message"]["content"].get<std::string>();
                json assistant_msg = {{"role", run_unit::settings["agent_name"]}, {"content", assistant_reply}};
                context.push_back(assistant_msg);

                // 思考过程
                json thinkings = json::array();
                if (think_mode)
                {
                    if (choices["message"].contains("reasoning_content"))
                        thinkings.push_back(choices["message"]["reasoning_content"]);
                    else if (choices["message"].contains("reasoning"))
                        thinkings.push_back(choices["message"]["reasoning"]);
                }

                // 工具/命令循环
                size_t mpc_count = 0;
                size_t max_rounds = run_unit::settings["max_mpc_rounds"].get<size_t>();
                std::string tool_call_content = assistant_reply;
                for (; mpc_count < max_rounds; ++mpc_count)
                {
                    std::string sys_out;
                    auto [tools_called, tools_ok] = tool_unit::tools_scan(tool_call_content, sys_out);
                    auto cs_called = cs_unit::cs_scan(tool_call_content, sys_out);
                    if (tools_called == 0 && cs_called == 0)
                        break;

                    // 构造工具/CS 输出消息
                    json sys_msg;
                    json sys_contents = json::array();
                    sys_contents.push_back({{"type", "text"}, {"text", sys_out}});
                    if (!tool_unit::image_queue.empty())
                    {
                        for (auto &img : tool_unit::image_queue)
                        {
                            sys_contents.push_back({{"type", "image_url"}, {"image_url", {{"url", img}}}});
                        }
                        tool_unit::image_queue.clear();
                    }
                    sys_msg = {{"role", "tool"}, {"content", sys_contents}};
                    context.push_back(sys_msg);

                    // 二次响应
                    llm_req["messages"] = context;
                    response.clear();
                    if (!client.generate(llm_req, response))
                    {
                        output = build_http_response(500, "application/json", R"({"error":"LLM call failed in tool loop"})");
                        return rt::FLAG_ERROR;
                    }
                    if (response.contains("usage"))
                    {
                        auto &usage = response["usage"];
                        if (usage.contains("prompt_tokens"))
                            total_prompt_tokens += usage["prompt_tokens"].get<size_t>();
                        if (usage.contains("completion_tokens"))
                            total_completion_tokens += usage["completion_tokens"].get<size_t>();
                    }
                    choices = response["choices"][0];
                    std::string new_reply = choices["message"]["content"].get<std::string>();
                    json new_assistant_msg = {{"role", run_unit::settings["agent_name"]}, {"content", new_reply}};
                    context.push_back(new_assistant_msg);
                    assistant_reply += "\r\n" + new_reply;
                    if (think_mode)
                    {
                        if (choices["message"].contains("reasoning_content"))
                            thinkings.push_back(choices["message"]["reasoning_content"]);
                        else if (choices["message"].contains("reasoning"))
                            thinkings.push_back(choices["message"]["reasoning"]);
                    }
                    tool_call_content = new_reply;
                }

                if (mpc_count >= max_rounds)
                {
                    assistant_reply += "\r\n[Max CS MPC CALL]\r\n";
                }

                json new_messages = json::array();
                // 保存所有会话消息（剔除头两条系统提示词）
                size_t history_start = 2 + (session_ptr->is_memory_empty() ? 0 : 1);
                for (size_t i = history_start; i < context.size(); ++i)
                {
                    std::string role = context[i]["role"];
                    if (role == "system" || role == "memory")
                        continue;
                    new_messages.push_back(context[i]);
                }
                session_ptr->messages = new_messages;

                // 保存到磁盘
                {
                    nlohmann::json save_msgs = nlohmann::json::array();
                    nlohmann::json asset_array = nlohmann::json::array();
                    for (auto &msg : session_ptr->messages)
                        save_msgs.push_back(run_unit::extract_images_from_message(msg, asset_array));
                    std::string ws2 = run_unit::settings["workspace"].get<std::string>();
                    tool_unit::writeFile(ws2 + "/sessions/" + session_ptr->session_id + ".json",
                                         save_msgs.dump(4));
                    if (!asset_array.empty())
                        tool_unit::writeFile(ws2 + "/assets/messages/" + session_ptr->session_id + ".json",
                                             asset_array.dump(4));
                    tool_unit::writeFile(ws2 + "/memorys/" + session_ptr->session_id + ".json",
                                         session_ptr->memory.dump(4));
                }

                assistant_reply += "\r\n" + std::format("\r\n[Context size: {} chars]", context.dump().length());
                json data = {
                    {"model", model},
                    {"thinkings", thinkings},
                    {"messages", new_messages},
                    {"stream", false},
                    {"think", think_mode},
                    {"usage", {{"prompt_cost", total_prompt_tokens}, {"completion_cost", total_completion_tokens}, {"total_cost", total_prompt_tokens + total_completion_tokens}}}};
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

                output = build_http_response(200, "application/json", data.dump());
                return rt::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error in handle_input_stream: " << e.what() << std::endl;
                webui_log("ERROR", "handle_input_stream", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }
        }
        void handle_input_stream_streaming(std::string &input, rt::WriteCallback write, const std::map<std::string, std::string> &params)
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
                    context.push_back({{"role", "memory"}, {"content", session_ptr->memory["abstracts"]}});

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

                    std::string response_text = client.stream_generate(
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

                    // 扫描工具/CS 调用
                    std::string sys_out;
                    auto [tools_called, tools_ok] = tool_unit::tools_scan(response_text, sys_out);
                    auto cs_called = cs_unit::cs_scan(response_text, sys_out);

                    if (tools_called == 0 && cs_called == 0)
                    {
                        json assistant_msg = {{"role", run_unit::settings["agent_name"]}, {"content", response_text}};
                        context.push_back(assistant_msg);
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
                }

                // 提取新消息并保存（剔除头两条系统提示词）
                json new_messages = json::array();
                size_t history_start = 2 + (session_ptr->is_memory_empty() ? 0 : 1);
                for (size_t i = history_start; i < context.size(); ++i)
                {
                    std::string role = context[i]["role"];
                    if (role == "system" || role == "memory")
                        continue;
                    new_messages.push_back(context[i]);
                }
                session_ptr->messages = new_messages;

                // 保存到磁盘
                {
                    nlohmann::json save_msgs = nlohmann::json::array();
                    nlohmann::json asset_array = nlohmann::json::array();
                    for (auto &msg : session_ptr->messages)
                        save_msgs.push_back(run_unit::extract_images_from_message(msg, asset_array));
                    std::string ws2 = run_unit::settings["workspace"].get<std::string>();
                    tool_unit::writeFile(ws2 + "/sessions/" + session_ptr->session_id + ".json",
                                         save_msgs.dump(4));
                    if (!asset_array.empty())
                        tool_unit::writeFile(ws2 + "/assets/messages/" + session_ptr->session_id + ".json",
                                             asset_array.dump(4));
                    tool_unit::writeFile(ws2 + "/memorys/" + session_ptr->session_id + ".json",
                                         session_ptr->memory.dump(4));
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
                sse({{"type", "done"}, {"usage", usage}, {"messages", new_messages}, {"thinkings", thinkings}});
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error in handle_input_stream_streaming: " << e.what() << std::endl;
                webui_log("ERROR", "handle_input_stream_streaming", e.what());
                try
                {
                    sse({{"type", "error"}, {"message", e.what()}});
                }
                catch (...)
                {
                }
            }
        }
    } // namespace server
} // namespace app

int main(int argc, char *argv[])
{
    try
    {
        std::cout << get_system_status() << std::endl;
        int port = 8080;
        std::string settings_path = "settings.json";
        std::string __pw = "";
        std::string apikey = "";
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "-p" || arg == "--port")
            {
                if (i + 1 < argc)
                    port = std::stoi(argv[++i]);
                else
                    throw std::runtime_error("Missing value after " + arg);
            }
            else if (arg == "--settings")
            {
                if (i + 1 < argc)
                    settings_path = argv[++i];
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