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
int system_check = run_unit::init_check("D:\\Developments\\CXX\\Agent.cpp\\settings.json");
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
    static size_t im_token_len = sizeof("<|im_start|>\n<|im_end|>") - 1;
    LLMProviders::LlamaClient client(run_unit::settings["server_address"].get_ref<const std::string &>());
    //LLMProviders::OpenAIClient client("https://api.deepseek.com", "sk-47b4754048ea433fa7e7c28ae2bf967e");

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
        uint8_t password_hash[SHA3_256_DIGEST_SIZE];
        if (!SHA3_256((const uint8_t *)password.c_str(), password.length(), password_hash))
        {
            return 1;
        }
        run_unit::settings.emplace("password", to_hex_string(password_hash, SHA3_256_DIGEST_SIZE));
        Admin = run_unit::settings["name"].get<std::string>();
        system_prompt = tool_unit::readFile(run_unit::settings["prompt_path"].get_ref<const std::string &>());
        replaceAll(system_prompt, "    ", "");
        replaceAll(system_prompt, "\r\n", "");
        run_unit::Agent_session_context.push_back(
            {
                {"role", "system"},
                {"content", system_prompt},
            });
        run_unit::Agent_session_context.push_back(
            {
                {"role", "system"},
                {"content", run_unit::tools_list.dump()},
            });
        return 0;
    }

    int save_memory(std::shared_ptr<run_unit::SessionContext> session_ptr, const std::string &model)
    {
        try
        {
            auto [aq, kq] = session_ptr->summary_query();
            nlohmann::json response;
            nlohmann::json req = {
                {"model", model},
                {"messages", {
                                 {
                                     {"role", "system"},
                                     {"content", aq},
                                 },
                             }},
                {"stream", false},
            };
            client.generate(req, response);
            session_ptr->memory["abstracts"] = std::move(response["choices"][0]["message"]["content"].get_ref<std::string &>());
            std::cout << "Abstracts generated successfully." << std::endl;
            req["messages"][0]["content"] = kq;
            client.generate(req, response);
            session_ptr->memory["keywords"] = std::move(response["choices"][0]["message"]["content"].get_ref<std::string &>());
            std::cout << "Keywords generated successfully." << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << '\n';
        }
        return 0;
    }

    namespace server
    {

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
        int handle_session_memory(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        // 路由注册函数
        void register_routes(rt::router &router)
        {
            router.on("/", handle_root);
            router.on("/api", handle_api_list);
            router.on("/api/models", handle_models);
            router.on("/api/settings", handle_settings);

            router.on("/api/control", handle_control);
            router.on("/api/input", handle_input_stream);

            router.on("/api/session", handle_session_list);
            router.on("/api/session/new", handle_new_session);
            router.on("/api/session/set", handle_session_set);
            router.on("/api/session/memory", handle_session_memory);
            router.on("/api/session/clear", handle_session_clear);
            // router.on("/api/session/delete", handle_delete_session);

            router.on("/api/login", handle_login);
            router.on("/api/logout", handle_logout);
            router.on("/api/loading", handle_loading_page);

            router.on("/api/channels", handle_channels_list);
            router.on("/api/tools", handle_tools_list);
            router.on("/api/todos", handle_todos_list);

            // auto router
            // router.on("/api/channels/:name", handle_channels_setting);
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
                html = tool_unit::readFile(run_unit::settings["webui"].get_ref<const std::string &>());
            }
            catch (const std::exception &e)
            {
                html = "<h1>Welcome</h1>\n<p>Error: ";
                html += e.what();
                html += "</p>";
            }
            output = build_http_response(200, "text/html", html);
            return rt::FLAG_DONE;
        }
        int handle_api_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            json api_list = {};
            output = build_http_response(200, "application/json", api_list.dump());
            return rt::FLAG_DONE;
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
            std::string channel;
            bool think_mode = false;
            size_t images_size = 0;
            std::shared_ptr<run_unit::SessionContext> session_ptr;
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
                    model = run_unit::settings["model"];
                }

                json contents = json::array();
                if (request["channel"] == nullptr)
                {
                    session_ptr = run_unit::agent_session_manager.get_current();
                    session_ptr->messages.push_back(Admin + ":" + user_message);
                    contents.push_back({{"type", "text"},
                                        {"text", user_message}});
                }
                else
                {
                    channel = request["channel"].get<std::string>();
                    session_ptr = run_unit::agent_session_manager.get(channel);
                    session_ptr->messages.push_back(Admin + "'s " + channel + ":" + user_message);
                    contents.push_back({{"type", "text"},
                                        {"text", "Messages received from " + channel + ":" + user_message}});
                }

                if (request["images"] != nullptr)
                {
                    for (auto &image : request["images"])
                    {
                        images_size += image.get_ref<std::string &>().size();
                        contents.push_back({{"type", "image_url"},
                                            {"image_url", {{"url", std::move(image.get_ref<std::string &>())}}}});
                    }
                }
                run_unit::ctx_lock.lock();
                run_unit::Agent_session_context.push_back({{"role", Admin},
                                                           {"content", std::move(contents)}});
                run_unit::ctx_lock.unlock();
                think_mode = request["think"].get<bool>();
                req = {
                    {"model", model},
                    {
                        "messages",
                        run_unit::Agent_session_context,
                    },
                    {"stream", false},
                    {"think", think_mode}};
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error: " << e.what() << std::endl;
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return rt::FLAG_ERROR;
            }

            json response;
            json thinkings = json::array();

            client.generate(req, response);
            auto choices = response["choices"][0];

            std::string _content = "\r\n" + choices["message"]["content"].get<std::string>();
            run_unit::ctx_lock.lock();
            run_unit::Agent_session_context.push_back({{"role", run_unit::settings["agent_nickname"]},
                                                       {"content", _content}});
            run_unit::ctx_lock.unlock();
            if (think_mode)
            {
                if (choices["message"].contains("reasoning_content"))
                {
                    thinkings.push_back(std::move(choices["message"]["reasoning_content"]));
                }
                else if (choices["message"].contains("reasoning"))
                {
                    thinkings.push_back(std::move(choices["message"]["reasoning"]));
                }
                else
                {
                    thinkings.push_back("Unknown key of reasoning.");
                }
            }

            std::string tool_call_content = _content;
            json images = json::array();
            size_t mpc_count = 0;
            for (; mpc_count < run_unit::settings["max_mpc_rounds"].get<size_t>(); mpc_count++)
            {
                try
                {
                    json sys_outs = json::array();
                    std::string sys_out;
                    bool tool_flag = tool_unit::tools_scan(tool_call_content, sys_out);
                    bool cs_flag = cs_unit::cs_scan(tool_call_content, sys_out);
                    if (cs_flag | tool_flag)
                    {
                        printf("Trigger command invocation at cs:%d tool: %d\n", cs_flag, tool_flag);
                        // prepare data
                        _content += "\r\n" + sys_out;
                        sys_outs.push_back({{"type", "text"},
                                            {"text", sys_out}});

                        if (!tool_unit::image_queue.empty())
                        {
                            for (auto &img : tool_unit::image_queue)
                            {
                                images_size += img.size();
                                sys_outs.push_back({{"type", "image_url"},
                                                    {"image_url", {{"url", img}}}});
                                images.push_back(std::move(img));
                            }
                            tool_unit::image_queue.clear();
                        }
                        run_unit::ctx_lock.lock();
                        run_unit::Agent_session_context.push_back({{"role", "system"},
                                                                   {"content", sys_outs}});
                        run_unit::ctx_lock.unlock();
                        req.clear();
                        req = {
                            {"model", model},
                            {
                                "messages",
                                run_unit::Agent_session_context,
                            },
                            {"stream", false},
                            {"think", think_mode}};

                        // tool response to agent
                        response.clear();
                        client.generate(req, response);
                        auto tool_choices = response["choices"][0];
                        tool_call_content = tool_choices["message"]["content"].get<std::string>();

                        _content += "\r\n" + tool_call_content;
                        if (think_mode)
                        {
                            if (tool_choices["message"].contains("reasoning_content"))
                            {
                                thinkings.push_back(std::move(tool_choices["message"]["reasoning_content"]));
                            }
                            else if (tool_choices["message"].contains("reasoning"))
                            {
                                thinkings.push_back(std::move(tool_choices["message"]["reasoning"]));
                            }
                            else
                            {
                                thinkings.push_back(std::move(tool_choices["message"]));
                            }
                        }

                        // updata session
                        run_unit::ctx_lock.lock();
                        run_unit::Agent_session_context.push_back({{"role", run_unit::settings["agent_nickname"]},
                                                                   {"content", tool_call_content}});
                        run_unit::ctx_lock.unlock();
                    }
                    else
                    {
                        break;
                    }
                }
                catch (const std::exception &e)
                {
                    _content += "\r\n[CS_Command_Error]\r\n";
                    std::cerr << e.what() << '\n';
                }
            }
            if (mpc_count >= run_unit::settings["max_mpc_rounds"].get<size_t>())
            {
                _content += "\r\n[Max CS MPC CALL]\r\n";
            }

            session_ptr->messages.push_back(run_unit::settings["agent_nickname"].get_ref<const std::string &>() + ":" + _content);
            size_t ctx_length = run_unit::Agent_session_context.dump().length() + (run_unit::Agent_session_context.size() * im_token_len) - images_size;
            if (ctx_length > run_unit::settings["max_context"].get<size_t>() && run_unit::over_ctx < ctx_length)
            {
                save_memory(session_ptr, model);
                run_unit::ctx_lock.lock();
                run_unit::Agent_session_context.clear();
                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "system"},
                        {"content", system_prompt},
                    });

                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "system"},
                        {"content", run_unit::tools_list.dump()},
                    });

                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "memory"},
                        {"content", session_ptr->memory["abstracts"]},
                    });
                std::cout << std::format("Session auto memory generated. ID:{}, num_messages:{}", session_ptr->session_id, session_ptr->messages.size()) << std::endl;
                run_unit::ctx_lock.unlock();
                ctx_length = run_unit::Agent_session_context.dump().length() + (run_unit::Agent_session_context.size() * im_token_len);
                run_unit::over_ctx = ctx_length;
            }

            _content += "\r\n" + std::format("\r\n[Number of characters (including im-tokens, excluding media file size): {}]", ctx_length);
            output = build_http_response(200, "application/json", json{{"model", model}, {"messages", {{{"type", "response"}, {"content", _content}}, {{"type", "think"}, {"content", thinkings}}, {{"type", "images"}, {"content", images}}}}, {"stream", false}, {"think", false}}.dump());
            return rt::FLAG_DONE;
        }
        int handle_session_clear(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            run_unit::ctx_lock.lock();
            run_unit::Agent_session_context.clear();
            run_unit::Agent_session_context.push_back(
                {
                    {"role", "system"},
                    {"content", system_prompt},
                });
            run_unit::Agent_session_context.push_back(
                {
                    {"role", "system"},
                    {"content", run_unit::tools_list.dump()},
                });
            run_unit::ctx_lock.unlock();

            run_unit::agent_session_manager.clear_current();

            json resp = {{"status", "cleared"}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                output = build_http_response(200, "application/json", client.models());
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
                {"session_list", run_unit::agent_session_manager.list_sessions()}};
            output = build_http_response(200, "application/json", resp.dump());
            return rt::FLAG_DONE;
        }
        int handle_new_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            run_unit::ctx_lock.lock();
            run_unit::Agent_session_context.clear();
            run_unit::Agent_session_context.push_back(
                {
                    {"role", "system"},
                    {"content", system_prompt},
                });

            run_unit::Agent_session_context.push_back(
                {
                    {"role", "system"},
                    {"content", run_unit::tools_list.dump()},
                });
            run_unit::ctx_lock.unlock();

            auto new_session = run_unit::agent_session_manager.create();
            run_unit::agent_session_manager.current_session_id = new_session->session_id;

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

                run_unit::agent_session_manager.remove_session(request["session_id"]);

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
                json input_data = json::parse(body);

                if (input_data["updata"].get<bool>() && input_data.contains("settings"))
                {
                    if (run_unit::validateJsonFormat(input_data["settings"]))
                    {
                        run_unit::settings = input_data["settings"];
                        tool_unit::writeFile(run_unit::setting_file_path, run_unit::settings.dump(4));
                        std::cout << "Warning - The settings file has been overwritten." << std::endl;
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
                std::cout << "Password: " << run_unit::settings["password"].get_ref<const std::string &>() << std::endl;

                // 简单验证逻辑
                if (username == run_unit::settings["name"].get_ref<const std::string &>() && password == run_unit::settings["password"].get_ref<const std::string &>())
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
            output = build_http_response(200, "application/json", run_unit::settings["channels"].dump());
            return rt::FLAG_DONE;
        }
        int handle_tools_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            output = build_http_response(200, "application/json", run_unit::tools_list.dump());
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
                for (auto &tool : run_unit::tools_list)
                {
                    if (tool["name"].get_ref<const std::string &>() == name)
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
                    if (todo["id"].get_ref<const std::string &>() == id)
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
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                run_unit::agent_session_manager.change_session(request["session_id"]);

                std::shared_ptr<run_unit::SessionContext> session_ptr = run_unit::agent_session_manager.get_current();

                run_unit::ctx_lock.lock();
                run_unit::Agent_session_context.clear();
                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "system"},
                        {"content", system_prompt},
                    });

                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "system"},
                        {"content", run_unit::tools_list.dump()},
                    });
                if (!session_ptr->is_memory_empty())
                {
                    run_unit::Agent_session_context.push_back(
                        {
                            {"role", "memory"},
                            {"content", session_ptr->memory["abstracts"]},
                        });
                    std::cout << std::format("Session memory loaded. ID:{}, num_messages:{}", session_ptr->session_id, session_ptr->messages.size()) << std::endl;
                }
                run_unit::ctx_lock.unlock();

                json resp = {{"status", "done"}, {"messages", session_ptr->messages}};
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

        int handle_session_memory(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                std::shared_ptr<run_unit::SessionContext> session_ptr = run_unit::agent_session_manager.get_current();
                run_unit::ctx_lock.lock();
                run_unit::Agent_session_context.clear();
                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "system"},
                        {"content", system_prompt},
                    });

                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "system"},
                        {"content", run_unit::tools_list.dump()},
                    });
                save_memory(session_ptr, run_unit::settings["model"].get<std::string>());

                run_unit::Agent_session_context.push_back(
                    {
                        {"role", "memory"},
                        {"content", session_ptr->memory["abstracts"]},
                    });

                run_unit::ctx_lock.unlock();

                json resp = {{"status", "done"}};
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
    } // server

} // namespace app

int main(int argc, char *argv[])
{
    try
    {
        // 可选启动Webview的GUI
        /*std::thread webview([]()
                            { system("python ui.py"); });*/
        app::init_app("88888888");

        boost::asio::io_context io_context;
        rt::router router;
        app::server::register_routes(router);
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