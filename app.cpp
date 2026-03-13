/**
 * @author Yauntyour (yauntyour@outlook.com)
 * @brief
 * @version 1.0
 * @date 2026-03-14
 *
 * @copyright Copyright (c) 2026
 *
 */
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
#include <nlohmann/json.hpp>

#include "servic.cpp/servic.hpp"
#include "servic.cpp/router.hpp"
#include "agent.hpp"
#include "servic.cpp/tiny_sha.h"

using json = nlohmann::json;

namespace app
{
    std::string loadagent()
    {
        try
        {
            return tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\agent.txt");
        }
        catch (const std::exception &e)
        {
            std::cerr << "Load agent.txt fail:" << e.what() << '\n';
        }
        return "";
    }
    static std::string Admin_name;
    static std::string Admin_call;
    static std::string Admin_password;
    static std::string system_prompt = loadagent();
    LLMProviders::OllamaClient client("http://localhost:11434");

    static std::mutex ctx_lock;
    static std::string Agent_session_context = system_prompt;
    std::string to_hex_string(const uint8_t *hash, size_t len)
    {
        std::stringstream ss;
        for (size_t i = 0; i < len; i++)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
    int init_password(const std::string &name, const std::string &password)
    {
        Admin_name = name;
        Admin_call = name + ":";
        uint8_t password_hash[SHA3_256_DIGEST_SIZE];
        if (!SHA3_256((const uint8_t *)password.c_str(), password.length(), password_hash))
        {
            return 1;
        }
        Admin_password = to_hex_string(password_hash, SHA3_256_DIGEST_SIZE);
        return 0;
    }

    // 会话状态结构
    struct SessionContext
    {
        std::string model = "qwen3.5:9b";
        std::vector<json> messages;
        bool thinking = false;
        std::string session_id;
        std::time_t created_at;

        SessionContext() : session_id(generate_uuid()), created_at(std::time(nullptr)) {}

        static std::string generate_uuid()
        {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 15);
            const char *hex = "0123456789abcdef";
            std::string uuid;
            for (int i = 0; i < 32; ++i)
            {
                if (i == 8 || i == 12 || i == 16 || i == 20)
                    uuid += "-";
                uuid += hex[dis(gen)];
            }
            return uuid;
        }
    };

    // 全局会话管理
    class SessionManager
    {

    private:
        std::unordered_map<std::string, std::shared_ptr<SessionContext>> sessions;
        std::unordered_map<std::string, std::shared_ptr<SessionContext>> archived_sessions;

    public:
        std::shared_ptr<SessionContext> get_or_create(const std::string &id)
        {
            auto it = sessions.find(id);
            if (it != sessions.end())
            {
                return it->second;
            }
            auto ctx = std::make_shared<SessionContext>();
            sessions[id] = ctx;
            current_session_id = id;
            return ctx;
        }

        void clear_current()
        {
            if (!current_session_id.empty())
            {
                sessions.erase(current_session_id);
                current_session_id.clear();
            }
        }

        void new_session(const std::string &old_id_hint = "")
        {
            if (!old_id_hint.empty() && sessions.count(old_id_hint))
            {
                archived_sessions[old_id_hint] = sessions[old_id_hint];
            }
            auto new_ctx = std::make_shared<SessionContext>();
            std::string new_id = new_ctx->session_id;
            sessions[new_id] = new_ctx;
            current_session_id = new_id;
        }

        std::vector<std::string> list_sessions() const
        {
            std::vector<std::string> result;
            for (const auto &pair : sessions)
            {
                result.push_back(pair.first);
            }
            return result;
        }

        std::string current_session_id;
    };

    SessionManager g_session_manager;

    // HTTP 响应构造函数
    std::string build_http_response(int status_code, const std::string &content_type, const std::string &body, bool cors = true)
    {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << " " << (status_code == 200 ? "OK" : "Not Found") << "\r\n";
        oss << "Content-Type: " << content_type << "\r\n";
        oss << "Content-Length: " << body.length() << "\r\n";
        if (cors)
        {
            oss << "Access-Control-Allow-Origin: *\r\n";
            oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            oss << "Access-Control-Allow-Headers: Content-Type\r\n";
        }
        oss << "\r\n";
        oss << body;
        return oss.str();
    }

    // 流式响应包装器（用于SSE）
    std::string build_sse_chunk(const std::string &data)
    {
        std::ostringstream oss;
        oss << "data: " << data << "\n\n";
        return oss.str();
    }

    // API 处理函数声明
    int handle_root(std::string &input, std::string &output);
    int handle_api_list(std::string &input, std::string &output);
    int handle_status(std::string &input, std::string &output);
    int handle_control(std::string &input, std::string &output);
    int handle_input_stream(std::string &input, std::string &output);
    int handle_sync_context(std::string &input, std::string &output);
    int handle_clear_session(std::string &input, std::string &output);
    int handle_models(std::string &input, std::string &output);
    int handle_new_session(std::string &input, std::string &output);
    int handle_delete_frontend_session(std::string &input, std::string &output);
    int handle_settings(std::string &input, std::string &output);
    int handle_loading_page(std::string &input, std::string &output);
    int handle_logout(std::string &input, std::string &output);
    int handle_login(std::string &input, std::string &output);
    int handle_channels_list(std::string &input, std::string &output);
    int handle_skills_list(std::string &input, std::string &output);
    int handle_todos_list(std::string &input, std::string &output);

    // 路由注册函数
    void register_routes(rt::router &router)
    {
        router.on("/", handle_root);
        router.on("/api", handle_api_list);
        router.on("/api/status", handle_status);
        router.on("/api/control", handle_control);
        router.on("/api/input", handle_input_stream);
        router.on("/api/syc", handle_sync_context);
        router.on("/api/clear", handle_clear_session);
        router.on("/api/models", handle_models);
        router.on("/api/new", handle_new_session);
        router.on("/api/delete", handle_delete_frontend_session);
        router.on("/api/settings", handle_settings);
        router.on("/api/loading", handle_loading_page);
        router.on("/api/logout", handle_logout);
        router.on("/api/login", handle_login);
        router.on("/api/channels", handle_channels_list);
        router.on("/api/skills", handle_skills_list);
        router.on("/api/todos", handle_todos_list);

        // 动态路由模式，这里简化处理
        router.on("/api/channels/<name>", [](std::string &input, std::string &output) -> int
                  {
            output = build_http_response(200, "application/json", R"({"status":"ok","channel":"default"})");
            return rt::FLAG_DONE; });

        router.on("/api/skills/<name>", [](std::string &input, std::string &output) -> int
                  {
            output = build_http_response(200, "application/json", R"({"status":"ok","skill":"active"})");
            return rt::FLAG_DONE; });

        router.on("/api/todos/<name>", [](std::string &input, std::string &output) -> int
                  {
            output = build_http_response(200, "application/json", R"({"status":"ok","todo":{"done":false}})");
            return rt::FLAG_DONE; });

        router.on("/api/todos/new", [](std::string &input, std::string &output) -> int
                  {
            output = build_http_response(200, "application/json", R"({"status":"created"})");
            return rt::FLAG_DONE; });

        router.on("/api/todos/delete", [](std::string &input, std::string &output) -> int
                  {
            output = build_http_response(200, "application/json", R"({"status":"deleted"})");
            return rt::FLAG_DONE; });
    }

    // 实现各API接口

    int handle_root(std::string &input, std::string &output)
    {
        std::string html;
        try
        {
            html = tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\webui.html");
        }
        catch (const std::exception &e)
        {
            html = "<h1>Welcome to AI Assistant</h1><p>Error: ";
            html += e.what();
        }
        output = build_http_response(200, "text/html", html);
        return rt::FLAG_DONE;
    }

    int handle_api_list(std::string &input, std::string &output)
    {
        json api_list = {
            {"/", "GET", "返回WebUI页面"},
            {"/api", "GET", "返回可用接口列表"},
            {"/api/status", "GET", "获取系统综合状态"},
            {"/api/control", "POST", "发送控制指令"},
            {"/api/input", "POST", "接收用户输入并流式响应"},
            {"/api/syc", "POST", "同步当前会话上下文"},
            {"/api/clear", "POST", "重启当前会话"},
            {"/api/models", "GET", "返回可用模型列表"},
            {"/api/new", "POST", "新建服务器会话"},
            {"/api/delete", "POST", "删除前端指定会话"},
            {"/api/settings", "POST", "更新服务器设置"},
            {"/api/loading", "GET", "返回登录页面"},
            {"/api/logout", "GET", "用户登出"},
            {"/api/login", "POST", "用户登录认证"},
            {"/api/channels", "GET", "返回频道列表"},
            {"/api/channels/<name>", "GET/POST", "查询或设置频道"},
            {"/api/skills", "GET", "返回技能列表"},
            {"/api/skills/<name>", "GET/POST", "查询或设置技能"},
            {"/api/todos", "GET", "返回待办事项列表"},
            {"/api/todos/<name>", "GET/POST", "查询或设置待办事项"},
            {"/api/todos/new", "POST", "新增待办事项"},
            {"/api/todos/delete", "POST", "删除待办事项"}};
        output = build_http_response(200, "application/json", api_list.dump(2));
        return rt::FLAG_DONE;
    }

    int handle_status(std::string &input, std::string &output)
    {
        try
        {
            std::string result = tool_unit::exec("cmd /c chcp 65001>nul && python.exe ./sys/sys_state.py 2>&1");
            json response = {
                {"raw_output", result},
                {"timestamp", std::time(nullptr)}};
            output = build_http_response(200, "application/json", response.dump());
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(500, "text/plain", "Failed to retrieve system status.");
            return rt::FLAG_ERROR;
        }
    }

    int handle_control(std::string &input, std::string &output)
    {
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                output = build_http_response(400, "text/plain", "Bad Request");
                return rt::FLAG_ERROR;
            }
            std::string body = input.substr(header_end + 4);
            json request = json::parse(body);
            std::string cmd = request.value("cmd", "");

            if (cmd == "restart")
            {
                g_session_manager.clear_current();
            }

            json response = {{"result", "success"}, {"executed", cmd}};
            output = build_http_response(200, "application/json", response.dump());
            return rt::FLAG_DONE;
        }
        catch (const std::exception &e)
        {
            json error = {{"error", e.what()}};
            output = build_http_response(400, "application/json", error.dump());
            return rt::FLAG_ERROR;
        }
    }

    int handle_input_stream(std::string &input, std::string &output)
    {
        json req;
        std::string model;
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                output = build_http_response(400, "text/plain", "Bad Request");
                return rt::FLAG_ERROR;
            }
            std::string body = input.substr(header_end + 4);
            json request = json::parse(body);

            std::string user_message = request["messages"].get<std::string>();
            model = request["model"].get<std::string>();

            auto session = g_session_manager.get_or_create(g_session_manager.current_session_id);
            session->messages.push_back({{"type", "request"}, {"content", user_message}});

            ctx_lock.lock();
            req = {
                {"model", model},
                {
                    "prompt",
                    Agent_session_context + Admin_call + user_message,
                },
                {"stream", true},
                {"think", false}};
            ctx_lock.unlock();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
            return rt::FLAG_ERROR;
        }

        std::string response;
        std::string thinking;
        client.stream_generate(req, response, thinking);
        std::string current = "\r\n" + thinking + "\r\n" + response + "\r\n";
        std::string sys_out;
        bool tool_flag = tool_unit::tools_scan(response, sys_out);
        bool cs_flag = cs_unit::cs_scan(response, sys_out);
        if (cs_flag | tool_flag)
        {
            printf("Trigger command invocation at cs:%d tool: %d\n", cs_flag, tool_flag);
            current += sys_out;
            req["prompt"] = req["prompt"].get<std::string>() + current;
            response.clear();
            thinking.clear();
            client.stream_generate(req, response, thinking);
            current += thinking + "\r\n" + response + "\r\n";
        }
        ctx_lock.lock();
        Agent_session_context += current;
        ctx_lock.unlock();
        output = build_http_response(200, "application/json", json{{"model", model}, {"messages", {{{"type", "response"}, {"content", current}}}}, {"stream", true}, {"think", false}}.dump());
        return rt::FLAG_DONE;
    }

    int handle_sync_context(std::string &input, std::string &output)
    {
        auto session = g_session_manager.get_or_create(g_session_manager.current_session_id);
        json resp = {
            {"session_id", session->session_id},
            {"model", session->model},
            {"messages", session->messages},
            {"thinking", session->thinking}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }

    int handle_clear_session(std::string &input, std::string &output)
    {
        ctx_lock.lock();
        Agent_session_context = system_prompt;
        ctx_lock.unlock();
        g_session_manager.clear_current();
        json resp = {{"status", "cleared"}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }

    int handle_models(std::string &input, std::string &output)
    {
        try
        {
            std::string result;
            net_unit::CURL_get(curl_easy_init(), "http://localhost:11434/api/tags", result);
            json response = json::parse(result);
            output = build_http_response(200, "application/json", response.dump());
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(500, "application/json", R"({"error":"cannot connect to Ollama"})");
            return rt::FLAG_ERROR;
        }
    }

    int handle_new_session(std::string &input, std::string &output)
    {
        g_session_manager.new_session();
        json resp = {
            {"status", "created"},
            {"new_session_id", g_session_manager.current_session_id}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }

    int handle_delete_frontend_session(std::string &input, std::string &output)
    {
        // 简化实现：仅清空内存中的会话
        g_session_manager.clear_current();
        json resp = {{"status", "frontend_deleted"}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }

    int handle_settings(std::string &input, std::string &output)
    {
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
            json settings = json::parse(body);

            // 此处可持久化到配置文件
            json resp = {{"status", "updated"}, {"settings", settings}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(400, "text/plain", "Invalid JSON");
            return rt::FLAG_ERROR;
        }
    }

    int handle_loading_page(std::string &input, std::string &output)
    {
        std::string html = R"(<!DOCTYPE html><html><head><title>Loading...</title></head><body><h2>Please Login</h2><form action="/api/login" method="post">User: <input type="text" name="user"><br>Pass: <input type="password" name="pass"><br><input type="submit" value="Login"></form></body></html>)";
        output = build_http_response(200, "text/html", html);
        return rt::FLAG_DONE;
    }

    int handle_logout(std::string &input, std::string &output)
    {
        g_session_manager.clear_current();
        output = build_http_response(200, "text/plain", "Logged out.");
        return rt::FLAG_DONE;
    }

    int handle_login(std::string &input, std::string &output)
    {
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                output = build_http_response(400, "text/plain", "Bad Request");
                return rt::FLAG_ERROR;
            }
            std::string body = input.substr(header_end + 4);

            // 解析
            size_t user_start = body.find("\"username\":\"") + 12;
            size_t user_end = body.find('"', user_start);
            std::string username = body.substr(user_start, user_end - user_start);

            size_t pass_start = body.find("\"password\":\"") + 12;
            size_t pass_end = body.find('"', pass_start);
            std::string password = body.substr(pass_start, pass_end - pass_start);

            std::cout << "Admin:" << Admin_password << std::endl;
            std::cout << " Login:" << username << " sha3-256:" << password << std::endl;

            // 简单验证逻辑
            if (username == "admin" && password == Admin_password)
            {
                json resp = {{"token", "fake-jwt-token"}, {"user", username}};
                output = build_http_response(200, "application/json", resp.dump());
            }
            else
            {
                output = build_http_response(401, "application/json", R"({"error":"invalid credentials"})");
            }
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(500, "text/plain", "Internal Error");
            return rt::FLAG_ERROR;
        }
    }

    int handle_channels_list(std::string &input, std::string &output)
    {
        json channels = json::array();
        channels.push_back({{"name", "general"}, {"status", "active"}, {"user_count", 1}});
        output = build_http_response(200, "application/json", channels.dump());
        return rt::FLAG_DONE;
    }

    int handle_skills_list(std::string &input, std::string &output)
    {
        json skills = {
            {"writing", true},
            {"analysis", true},
            {"coding", true}};
        output = build_http_response(200, "application/json", skills.dump());
        return rt::FLAG_DONE;
    }

    int handle_todos_list(std::string &input, std::string &output)
    {
        json todos = json::array();
        todos.push_back({{"id", 1}, {"title", "Complete report"}, {"done", false}});
        output = build_http_response(200, "application/json", todos.dump());
        return rt::FLAG_DONE;
    }

} // namespace app

int main(int argc, char *argv[])
{
    try
    {
        app::init_password("admin", "admin");

        boost::asio::io_context io_context;
        rt::router router;
        app::register_routes(router);
        servic::Server server(io_context, 8080);
        server.run(router);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}