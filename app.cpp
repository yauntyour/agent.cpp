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
    static json setting = json::parse(tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\setting.json"));
    static json::array_t tools_setting = json::parse(tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\tools/tools.json"));
    static std::string Admin_call;
    static std::string system_prompt;
    LLMProviders::OllamaClient client(setting["ollama_server"].get_ref<std::string &>());

    static std::mutex ctx_lock;
    static std::string Agent_session_context;
    std::vector<std::string> get_all_files(const std::string &root_dir)
    {
        std::vector<std::string> files;

        // 检查目录是否存在
        if (!fs::exists(root_dir) || !fs::is_directory(root_dir))
        {
            std::cerr << "无效目录: " << root_dir << std::endl;
            return files;
        }

        // recursive_directory_iterator 会递归遍历子目录
        // directory_iterator 只遍历当前目录
        for (const auto &entry : fs::recursive_directory_iterator(root_dir))
        {
            if (entry.is_regular_file())
            { // 确保是普通文件，排除目录、符号链接等
                files.push_back(entry.path().string());
            }
        }

        return files;
    }
    auto file_parse(const std::string &file_path)
    {
        fs::path p(file_path);
        return std::make_pair(p.stem().string(), p.extension().string());
    }
    std::string to_hex_string(const uint8_t *hash, size_t len)
    {
        std::stringstream ss;
        for (size_t i = 0; i < len; i++)
        {
            // 以 16 进制、2位宽度、不足补0 的格式写入
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
    int init_app(const std::string &password = "")
    {
        if (password != "")
        {
            uint8_t password_hash[SHA3_256_DIGEST_SIZE];
            if (!SHA3_256((const uint8_t *)password.c_str(), password.length(), password_hash))
            {
                return 1;
            }
            setting["password"] = to_hex_string(password_hash, SHA3_256_DIGEST_SIZE);
        }
        Admin_call = setting["name"].get<std::string>() + ":";
        system_prompt = tool_unit::readFile(setting["prompt_path"].get_ref<std::string &>());
        Agent_session_context = system_prompt;
        return 0;
    }

    static json tools_list = json::array();

    struct SessionContext
    {
        json messages = json::array();
        ;
        bool thinking = false;
        std::string session_id;
        SessionContext()
        {
            session_id = std::to_string(std::time(nullptr));
        }
        SessionContext(const std::string &session_id) : session_id(session_id)
        {
        }
    };

    class SessionManager
    {
    public:
        std::unordered_map<std::string, std::shared_ptr<SessionContext>> sessions;
        std::string current_session_id = "";
        std::shared_ptr<SessionContext> get(const std::string &id)
        {
            auto it = sessions.find(id);
            if (it != sessions.end())
            {
                return it->second;
            }
            return nullptr;
        }
        std::shared_ptr<SessionContext> create()
        {
            auto session = std::make_shared<SessionContext>();
            sessions[session->session_id] = session;
            current_session_id = session->session_id;
            return session;
        }
        std::shared_ptr<SessionContext> get_current()
        {
            if (current_session_id == "")
            {
                return create();
            }
            return get(current_session_id);
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
        void clear_current()
        {
            auto ses = get_current();
            ses->messages.clear();
        }

        void remove_session(const std::string &id)
        {
            sessions.erase(id);
        }
        void change_session(const std::string &id)
        {
            current_session_id = id;
        }
        SessionManager()
        {
            try
            {
                auto load_sessions = get_all_files(setting["workspace"].get_ref<std::string &>() + "/sessions");
                for (auto &session : load_sessions)
                {
                    auto [name, ext] = file_parse(session);
                    if (ext == ".json")
                    {
                        auto ses_obj = std::make_shared<SessionContext>(name);
                        ses_obj->messages = json::parse(tool_unit::readFile(session));
                        sessions[name] = ses_obj;
                    }
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
        ~SessionManager()
        {
            try
            {
                for (auto &ses : sessions)
                {
                    std::cout << "Saving session: " << ses.first << std::endl;
                    tool_unit::writeFile("D:\\Developments\\CXX\\Agent.cpp\\sessions\\" + ses.first + ".json", ses.second->messages.dump());
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << e.what() << '\n';
            }
        }
    };

    SessionManager agent_session_manager;

    int scan_tools()
    {
        try
        {
            tools_list.clear();
            auto tools = get_all_files(setting["workspace"].get_ref<std::string &>() + "/tools");
            for (auto &tool : tools)
            {
                auto [name, ext] = file_parse(tool);
                if (ext == ".md")
                {
                    std::ifstream file(tool);
                    std::string description;
                    std::getline(file, description, '\n');
                    tools_list.push_back(json{
                        {"name", name},
                        {"description", description},
                    });
                }
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return 1;
        }
        return 0;
    }
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

    // API 处理函数声明
    int handle_root(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_api_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_status(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_control(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_input_stream(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_session_clear(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_new_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_delete_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_settings(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

    int handle_loading_page(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_logout(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_login(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

    int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_tools_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_todos_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_session_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

    int handle_channels_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_tools_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_todos_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_todos_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_todos_new(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
    int handle_session_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

    // 路由注册函数
    void register_routes(rt::router &router)
    {
        router.on("/", handle_root);
        router.on("/api", handle_api_list);
        router.on("/api/status", handle_status);
        router.on("/api/models", handle_models);
        router.on("/api/settings", handle_settings);

        router.on("/api/control", handle_control);
        router.on("/api/input", handle_input_stream);

        router.on("/api/session", handle_session_list);
        router.on("/api/new", handle_new_session);
        router.on("/api/clear", handle_session_clear);
        router.on("/api/delete", handle_delete_session);
        router.on("/api/session/set", handle_session_set);

        router.on("/api/login", handle_login);
        router.on("/api/logout", handle_logout);
        router.on("/api/loading", handle_loading_page);

        router.on("/api/channels", handle_channels_list);
        router.on("/api/tools", handle_tools_list);
        router.on("/api/todos", handle_todos_list);

        // auto router
        router.on("/api/channels/:name", handle_channels_setting);
        router.on("/api/tools/:name", handle_tools_setting);

        // todos
        router.on("/api/todos/:id", handle_todos_setting);
        router.on("/api/todos/new", handle_todos_new);
        router.on("/api/todos/delete/:id", handle_todos_delete);
    }

    // 实现各API接口

    int handle_root(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        std::string html;
        try
        {
            html = tool_unit::readFile(setting["webui"].get_ref<std::string &>());
        }
        catch (const std::exception &e)
        {
            html = "<h1>Welcome to AI Assistant</h1>\n<p>Error: ";
            html += e.what();
            html += "</p>";
        }
        output = build_http_response(200, "text/html", html);
        return rt::FLAG_DONE;
    }
    int handle_api_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
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
        output = build_http_response(200, "application/json", api_list.dump());
        return rt::FLAG_DONE;
    }
    int handle_status(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
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
    int handle_control(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
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
            // 执行控制指令
            json response = {{"result", "success"}, {"executed", "null"}};
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
    int handle_input_stream(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        json req;
        std::string model;
        bool is_webui = true;
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
            if (model == "default")
            {
                model = setting["ollama_default_model"];
            }

            if (request["channel"] == nullptr)
            {
                agent_session_manager.get_current()->messages.push_back(Admin_call + user_message);
                is_webui = false;
            }

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
            req["images"] = json::array();
            if (request["images"] != nullptr)
            {
                for (auto &image : request["images"])
                {
                    req["images"].push_back(std::move(image.get_ref<std::string &>()));
                }
            }
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
        req["images"].clear();
        std::string current = "\r\n" + thinking + "\r\n" + response + "\r\n";
        std::string sys_out;
        bool tool_flag = tool_unit::tools_scan(response, sys_out);
        bool cs_flag = cs_unit::cs_scan(response, sys_out);
        if (cs_flag | tool_flag)
        {
            printf("Trigger command invocation at cs:%d tool: %d\n", cs_flag, tool_flag);
            current += sys_out;
            req["prompt"] = req["prompt"].get<std::string>() + current;

            if (!tool_unit::image_queue.empty())
            {
                for (auto &img : tool_unit::image_queue)
                {
                    req["images"].push_back(std::move(img));
                }
                tool_unit::image_queue.clear();
            }

            response.clear();
            thinking.clear();
            client.stream_generate(req, response, thinking);
            current += thinking + "\r\n" + response + "\r\n";
        }
        ctx_lock.lock();
        Agent_session_context += current;
        ctx_lock.unlock();
        if (is_webui)
        {
            agent_session_manager.get_current()->messages.push_back("context:" + current);
        }

        size_t ctx_length = Agent_session_context.length();

        current += std::format("\r\n[context length now: {}]", ctx_length);

        output = build_http_response(200, "application/json", json{{"model", model}, {"messages", {{{"type", "response"}, {"content", current}}}}, {"stream", true}, {"think", false}}.dump());
        return rt::FLAG_DONE;
    }
    int handle_session_clear(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        ctx_lock.lock();
        Agent_session_context = system_prompt;
        ctx_lock.unlock();

        agent_session_manager.clear_current();

        json resp = {{"status", "cleared"}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }
    int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        try
        {
            std::string result;
            net_unit::CURL_get(curl_easy_init(), "http://localhost:11434/api/tags", result);
            output = build_http_response(200, "application/json", result);
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(500, "application/json", R"({"error":"cannot connect to Ollama"})");
            return rt::FLAG_ERROR;
        }
    }
    int handle_session_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        json resp = {
            {"session_list", agent_session_manager.list_sessions()}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }
    int handle_new_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        ctx_lock.lock();
        Agent_session_context = system_prompt;
        ctx_lock.unlock();

        auto new_session = agent_session_manager.create();
        agent_session_manager.current_session_id = new_session->session_id;

        json resp = {{"status", "OK"}, {"session_id", new_session->session_id}};
        output = build_http_response(200, "application/json", resp.dump());
        return rt::FLAG_DONE;
    }
    int handle_delete_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
            json request = json::parse(body);

            agent_session_manager.remove_session(request["session_id"]);

            json resp = {{"status", "deleted"}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(500, "text/plain", "Cannot delete session.");
            return rt::FLAG_ERROR;
        }
    }
    int handle_settings(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";

            json input_setting = json::parse(body);
            setting["stream"] = input_setting["stream"];
            output = build_http_response(200, "application/json", "{\"status\": \"OK\"}");
            return rt::FLAG_DONE;
        }
        catch (...)
        {
            output = build_http_response(500, "text/plain", "Settings saved failed.");
            return rt::FLAG_ERROR;
        }
    }
    int handle_loading_page(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        std::string html = R"(<!DOCTYPE html><html><head><title>Loading...</title></head><body><h2>Please Login</h2><form action="/api/login" method="post">User: <input type="text" name="user"><br>Pass: <input type="password" name="pass"><br><input type="submit" value="Login"></form></body></html>)";
        output = build_http_response(200, "text/html", html);
        return rt::FLAG_DONE;
    }
    int handle_logout(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        // 删除 token
        output = build_http_response(200, "text/plain", "Logged out.");
        return rt::FLAG_DONE;
    }
    int handle_login(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
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

            std::cout << "Login: " << username << " " << password << std::endl;
            std::cout << "Password: " << setting["password"].get_ref<std::string &>() << std::endl;

            // 简单验证逻辑
            if (username == setting["name"].get_ref<std::string &>() && password == setting["password"].get_ref<std::string &>())
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
    int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        json channels = json::array();
        channels.push_back({{"name", "general"}, {"status", "active"}, {"user_count", 1}});
        output = build_http_response(200, "application/json", channels.dump());
        return rt::FLAG_DONE;
    }
    int handle_tools_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        if (scan_tools())
        {
            output = build_http_response(200, "application/json", "{}");
        }
        else
        {
            output = build_http_response(200, "application/json", tools_list.dump());
        }
        return rt::FLAG_DONE;
    }
    int handle_todos_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        try
        {
            json::array_t todos = json::parse(tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json"));
            output = build_http_response(200, "application/json", json(todos).dump());
            return rt::FLAG_DONE;
        }
        catch (const std::exception &e)
        {
            output = build_http_response(500, "application/json", "[]");
            std::cerr << e.what() << '\n';
            return rt::FLAG_ERROR;
        }
    }
    int handle_channels_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        return 0;
    }
    int handle_tools_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        if (params.size() > 0)
        {
            std::string name = params.count("name") ? params.at("name") : "unknown";
            for (auto &tool : tools_setting)
            {
                if (tool["name"].get_ref<std::string &>() == name)
                {
                    output = build_http_response(200, "application/json", tool.dump());
                }
            }
        }
        else
        {
            output = build_http_response(400, "application/json", "{}");
        }
        return 0;
    }
    int handle_todos_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        try
        {
            size_t header_end = input.find("\r\n\r\n");
            std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
            json request = json::parse(body);
            std::string id = params.count("id") ? params.at("id") : "unknown";
            json::array_t todos = json::parse(tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json"));
            for (auto &todo : todos)
            {
                if (todo["id"].get_ref<std::string &>() == id)
                {
                    todo = request;
                    tool_unit::writeFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json", json(todos).dump());
                    output = build_http_response(200, "application/json", "{}");
                    return rt::FLAG_DONE;
                }
            }
            output = build_http_response(404, "application/json", "{}");
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            output = build_http_response(500, "application/json", "{}");
            return rt::FLAG_ERROR;
        }
        return 0;
    }
    int handle_todos_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        std::string id = params.count("id") ? params.at("id") : "unknown";
        json::array_t todos = json::parse(tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json"));
        todos.erase(std::remove_if(todos.begin(), todos.end(), [&](const json &todo)
                                   { return todo["id"].get_ref<const std::string &>() == id; }),
                    todos.end());
        tool_unit::writeFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json", json(todos).dump());
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

            json::array_t todos = json::parse(tool_unit::readFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json"));
            todos.push_back(request);
            tool_unit::writeFile("D:\\Developments\\CXX\\Agent.cpp\\todos.json", json(todos).dump());
            output = build_http_response(200, "application/json", request.dump());
            return rt::FLAG_DONE;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
            return rt::FLAG_ERROR;
        }
    }
    int handle_session_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
    {
        try
        {
            ctx_lock.lock();
            Agent_session_context = system_prompt;
            ctx_lock.unlock();

            size_t header_end = input.find("\r\n\r\n");
            std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
            json request = json::parse(body);
            agent_session_manager.change_session(request["session_id"]);
            json resp = {{"status", "done"}, {"messages", agent_session_manager.get_current()->messages}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        catch (const std::exception &e)
        {
            json resp = {{"status", "failed"}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_ERROR;
        }
    }
} // namespace app

int main(int argc, char *argv[])
{
    try
    {
        app::init_app("88888888");

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