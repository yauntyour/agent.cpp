// ============================================================================
//  server/routes.cpp —— HTTP 路由层
//
//  所有 REST 接口与 SSE 流式接口都在这里注册与实现，处理器统一返回
//  http::FLAG_DONE / FLAG_ERROR。静态资源（WebUI）由 webui 模块兜底。
// ============================================================================
#include "server/routes.hpp"

#include "server/webui.hpp"
#include "server/http_server.hpp"
#include "core/log.hpp"
#include "core/config.hpp"
#include "agent/runtime.hpp"
#include "agent/memory.hpp"
#include "agent/providers.hpp"
#include "agent/tools.hpp"
#include "agent/permissions.hpp"
#include "agent/commands.hpp"
#include "channels/qq.hpp"
#include "channels/channel.hpp"
#include "channels/common.hpp"

namespace server
{
    // 兼容旧处理函数的响应构造
    inline std::string build_http_response(int status_code, const std::string &content_type,
                                           const std::string &body, bool cors = true)
    {
        return http::build_response(status_code, content_type, body, {}, cors);
    }

    inline void webui_log(const std::string &level, const std::string &context,
                          const std::string &message)
    {
        logx::write(level, context, message);
    }

    // 工作区绝对路径（settings.workspace 可能是相对 .agent 的写法）
    inline std::string ws_path()
    {
        return paths::to_utf8(config::workspace_dir());
    }

    namespace
    {
        std::string trim_str(std::string s)
        {
            const char *ws = " \t\r\n";
            size_t b = s.find_first_not_of(ws);
            if (b == std::string::npos)
                return "";
            size_t e = s.find_last_not_of(ws);
            return s.substr(b, e - b + 1);
        }

        json *channel_by_name(const std::string &name)
        {
            if (!run_unit::settings.contains("channels") || !run_unit::settings["channels"].is_array())
                return nullptr;
            for (auto &ch : run_unit::settings["channels"])
                if (ch.value("name", "") == name)
                    return &ch;
            return nullptr;
        }

        bool ensure_channel_array()
        {
            if (!run_unit::settings.contains("channels") || !run_unit::settings["channels"].is_array())
                run_unit::settings["channels"] = json::array();
            return true;
        }
    } // namespace

    using agent::chat_mutex;
    using agent::client_generate;
    using agent::client_models;
    using agent::client_stream_generate;
    using agent::find_provider;
    using agent::provider_to_string;
    using agent::save_memory;
    using agent::switch_provider_by_id;
    using agent::channel_chat;
    using agent::maybe_auto_summarize;

        int handle_root(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_static(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_webui_info(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_channel_drivers(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_qq_event(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
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
        int handle_channel_stop(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        // ——— 频道增删改（频道管理器直接写配置，不再需要前端拼整份 settings） ———
        int handle_channels_add(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_channels_update(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_channels_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

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

        // ——— 工具权限系统 API ———
        int handle_permissions_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_permissions_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_permissions_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_permission_respond(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        int handle_fs_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params);
        int handle_fs_used(std::string &input, std::string &output, const std::map<std::string, std::string> &params);

        void handle_input_streaming(std::string &input, http::WriteCallback write, const std::map<std::string, std::string> &params);

        void register_routes(http::Router &router)
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
            router.on("/api/channels/add", handle_channels_add);       // POST — 新建频道
            router.on("/api/channels/update", handle_channels_update); // POST — 修改类型/状态/参数
            router.on("/api/channels/delete", handle_channels_delete); // POST — 删除频道
            router.on("/api/channel/token", handle_channel_token_set);       // POST — 加密存储
            router.on("/api/channel/token/:name", handle_channel_token_get); // GET — 解密读取
            router.on("/api/channel/qr/:name", handle_channel_qr);           // GET — 登录二维码/状态
            router.on("/api/channel/status", handle_channel_status);         // GET — 全部 Bot 状态
            router.on("/api/channel/start", handle_channel_start);           // POST — 运行时启动指定频道 Bot
            router.on("/api/channel/stop", handle_channel_stop);             // POST — 停止指定频道 Bot
            router.on("/api/tools", handle_tools_list);
            router.on("/api/tools/toggle", handle_tools_toggle);

            // 工具权限系统 API
            router.on("/api/permissions", handle_permissions_get);        // GET — 规则/默认策略/待审批队列
            router.on("/api/permissions/set", handle_permissions_set);    // POST — 新增/更新规则
            router.on("/api/permissions/delete", handle_permissions_delete); // POST — 删除规则
            router.on("/api/permission/respond", handle_permission_respond); // POST — 响应待审批请求
            router.on("/api/fs/list", handle_fs_list);
            router.on("/api/fs/used", handle_fs_used);
            router.on("/api/todos", handle_todos_list);
            router.on("/api/todos/new", handle_todos_new);
            router.on("/api/todos/delete/:id", handle_todos_delete);
            router.on("/api/todos/:id", handle_todos_setting);

            // 流式路由
            router.on_stream("/api/input/stream", handle_input_streaming);

            // ——— WebUI 静态资源与运行时诊断 ———
            router.on("/api/webui", handle_webui_info);
            router.on("/api/channels/drivers", handle_channel_drivers);
            // QQ（OneBot v11）事件反向上报：路径前缀可在频道配置里改
            router.on_prefix("/api/channel/qq/", handle_qq_event);
            // 未命中的路径交给 WebUI 静态资源（多文件模式下的 app.css / app.js / assets/…）
            router.fallback(handle_static);
        }

        // ════════════════════════════════════════════════════════════════
        // WebUI 与运行时诊断
        // ════════════════════════════════════════════════════════════════
        int handle_root(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            (void)params;
            return webui::handle(input, output, params);
        }

        int handle_static(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            (void)params;
            const std::string path = http::Router::normalize(http::extract_url(input));
            if (path.rfind("/api/", 0) == 0)
            {
                output = build_http_response(404, "application/json", R"({"error":"not found"})");
                return http::FLAG_DONE;
            }
            return webui::handle(input, output, params);
        }

        int handle_webui_info(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            (void)input;
            (void)params;
            output = build_http_response(200, "application/json", webui::info().dump());
            return http::FLAG_DONE;
        }

        int handle_channel_drivers(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            (void)input;
            (void)params;
            json arr = json::array();
            for (auto &t : channels::driver_types())
            {
                const channels::Driver *d = channels::find_driver(t);
                if (!d)
                    continue;
                // 新建频道时前端要一份可用的 config 初值：取 settings 默认值里同类型的频道
                json cfg = json::object();
                for (auto &c : config::defaults().value("channels", json::array()))
                    if (c.value("type", "") == d->type && c.contains("config"))
                    {
                        cfg = c["config"];
                        break;
                    }
                arr.push_back({{"type", d->type},
                               {"display", d->display},
                               {"hint", d->hint},
                               {"config", cfg},
                               {"needs_token", d->type != "qq"}});
            }
            output = build_http_response(200, "application/json", json{{"drivers", arr}}.dump());
            return http::FLAG_DONE;
        }

        // QQ OneBot v11 反向上报入口
        int handle_qq_event(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            (void)params;
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end == std::string::npos) ? "" : input.substr(header_end + 4);
                if (body.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"empty body"})");
                    return http::FLAG_DONE;
                }
                json ev = json::parse(body);

                // 找到目标频道：优先按 URL 里的名字，其次按 self_id 匹配
                std::string target;
                const std::string path = http::Router::normalize(http::extract_url(input));
                // /api/channel/qq/<name>/event
                const std::string prefix = "/api/channel/qq/";
                if (path.size() > prefix.size())
                {
                    std::string rest = path.substr(prefix.size());
                    size_t slash = rest.find('/');
                    std::string seg = (slash == std::string::npos) ? rest : rest.substr(0, slash);
                    if (!seg.empty() && seg != "event")
                        target = seg;
                }

                const std::string self_id = ev.contains("self_id") ? std::to_string(ev.value("self_id", 0LL)) : "";
                if (run_unit::settings.contains("channels"))
                {
                    for (auto &ch : run_unit::settings["channels"])
                    {
                        if (ch.value("type", "") != "qq" && ch.value("path", "").find("qq") == std::string::npos)
                            continue;
                        json cfg = ch.value("config", json::object());
                        if (target.empty() && !self_id.empty() && cfg.value("self_id", "") == self_id)
                            target = ch.value("name", "");
                        if (target.empty() && cfg.value("self_id", "").empty())
                            target = ch.value("name", "");
                    }
                }
                if (target.empty())
                {
                    output = build_http_response(404, "application/json", R"({"error":"no qq channel"})");
                    return http::FLAG_DONE;
                }

                bot::qq::push_event(target, ev);
                output = build_http_response(200, "application/json", R"({"status":"ok"})");
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_qq_event", e.what());
                output = build_http_response(400, "application/json",
                                             json{{"error", e.what()}}.dump());
                return http::FLAG_DONE;
            }
        }

        int handle_models(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                output = build_http_response(200, "application/json", client_models());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_models", e.what());
                output = build_http_response(500, "application/json", R"({"error":"cannot connect to Ollama"})");
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_models", "unknown error");
                output = build_http_response(500, "application/json", R"({"error":"cannot connect to Ollama"})");
                return http::FLAG_ERROR;
            }
        }
        int handle_session_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            json resp = {{"session_list", run_unit::agent_session_manager.list_sessions()}};
            output = build_http_response(200, "application/json", resp.dump());
            return http::FLAG_DONE;
        }
        int handle_new_session(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            auto new_session = run_unit::agent_session_manager.create();
            json resp = {{"status", "OK"}, {"session_id", new_session->session_id}};
            output = build_http_response(200, "application/json", resp.dump());
            return http::FLAG_DONE;
        }
        int handle_session_clear(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            run_unit::agent_session_manager.clear_current();
            json resp = {{"status", "cleared"}};
            output = build_http_response(200, "application/json", resp.dump());
            return http::FLAG_DONE;
        }
        int handle_session_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
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
                    return http::FLAG_ERROR;
                }

                // 从内存移除
                run_unit::agent_session_manager.remove_session(session_id);

                // 删除磁盘文件
                std::string ws = ws_path();
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
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_session_delete", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_session_get_msg(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
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
                    return http::FLAG_ERROR;
                }
                run_unit::agent_session_manager.change_session(session_id);
                bool has_memory = !session->is_memory_empty();
                json resp = {{"messages", session->messages}, {"memory", has_memory}};
                if (has_memory)
                    resp["memory_created_at"] = session->memory["created_at"];
                output = build_http_response(200, "application/json", resp.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_session_get_msg", e.what());
                output = build_http_response(500, "application/json", "{}");
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_session_get_msg", "unknown error");
                output = build_http_response(500, "application/json", "{}");
                return http::FLAG_ERROR;
            }
        }
        int handle_session_memory(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                auto session = run_unit::agent_session_manager.get_current();
                save_memory(session, run_unit::settings["model"].get<std::string>());
                json resp = {{"status", "done"}};
                resp["memory_created_at"] = session->memory["created_at"];
                output = build_http_response(200, "application/json", resp.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_session_memory", e.what());
                output = build_http_response(500, "application/json", json{{"status", "failed"}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_settings(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json input_data = json::object();
                if (!body.empty())
                {
                    try
                    {
                        input_data = json::parse(body);
                    }
                    catch (...)
                    {
                        input_data = json::object();
                    }
                }
                if (!input_data.is_object())
                    input_data = json::object();

                if (input_data.value("updata", false) && input_data.contains("settings"))
                {
                    // 深合并：前端只提交它认识的字段，webui / server / root 等
                    // 未提交的配置必须原样保留
                    std::string err;
                    if (!config::apply(input_data["settings"], err))
                    {
                        output = build_http_response(400, "application/json",
                                                     json{{"error", err}}.dump());
                        return http::FLAG_ERROR;
                    }
                }
                output = build_http_response(200, "application/json", run_unit::settings.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_settings", e.what());
                output = build_http_response(500, "text/plain", "Settings saved failed.");
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_settings", "unknown error");
                output = build_http_response(500, "text/plain", "Settings saved failed.");
                return http::FLAG_ERROR;
            }
        }
        int handle_data(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                output = build_http_response(200, "application/json", run_unit::agent_data_manager.data.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_data", e.what());
                output = build_http_response(500, "application/json", R"({"error":"Fail to get data"})");
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_data", "unknown error");
                output = build_http_response(500, "application/json", R"({"error":"Fail to get data"})");
                return http::FLAG_ERROR;
            }
        }
        int handle_provider(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string provider_id = request.value("provider_id", "");
                if (provider_id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing provider_id"})");
                    return http::FLAG_ERROR;
                }

                if (!switch_provider_by_id(provider_id))
                {
                    output = build_http_response(404, "application/json", R"({"error":"provider not found"})");
                    return http::FLAG_ERROR;
                }

                json resp = {
                    {"status", "OK"},
                    {"provider_id", provider_id}};
                output = build_http_response(200, "application/json", resp.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_provider", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // —————————————— 供应商管理 API ——————————————

        // GET /api/providers → 返回所有供应商列表
        int handle_providers_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                json resp = {
                    {"providers", run_unit::settings.value("providers", json::array())},
                    {"current_provider", run_unit::settings.value("current_provider", "")}};
                output = build_http_response(200, "application/json", resp.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_list", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // POST /api/providers/add → 添加新供应商
        int handle_providers_add(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
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
                    return http::FLAG_ERROR;
                }

                // 检查 id 是否已存在
                if (!run_unit::settings.contains("providers"))
                    run_unit::settings["providers"] = json::array();
                for (auto &p : run_unit::settings["providers"])
                {
                    if (p.value("id", "") == id)
                    {
                        output = build_http_response(409, "application/json", R"({"error":"provider id already exists"})");
                        return http::FLAG_ERROR;
                    }
                }

                // 如果提供了 api_key，加密存储到文件
                if (!api_key.empty())
                {
                    std::string ws = ws_path();
                    std::string key_dir = ws + "/tokens/providers";
                    std::filesystem::create_directories(key_dir);
                    std::string file_path = key_dir + "/" + id + ".enc";
                    std::string encrypted = crypto_unit::encrypt(api_key, crypto_context::key());
                    if (encrypted.empty())
                    {
                        output = build_http_response(500, "application/json", R"({"error":"encryption failed"})");
                        return http::FLAG_ERROR;
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
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_add", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // POST /api/providers/update → 更新供应商配置
        int handle_providers_update(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string id = request.value("id", "");
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return http::FLAG_ERROR;
                }

                json *provider = find_provider(id);
                if (!provider)
                {
                    output = build_http_response(404, "application/json", R"({"error":"provider not found"})");
                    return http::FLAG_ERROR;
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
                    std::string ws = ws_path();
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
                            return http::FLAG_ERROR;
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
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_update", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // POST /api/providers/delete → 删除供应商
        int handle_providers_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);

                std::string id = request.value("id", "");
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return http::FLAG_ERROR;
                }

                if (!run_unit::settings.contains("providers"))
                {
                    output = build_http_response(404, "application/json", R"({"error":"no providers"})");
                    return http::FLAG_ERROR;
                }

                // 不允许删除最后一个供应商
                if (run_unit::settings["providers"].size() <= 1)
                {
                    output = build_http_response(400, "application/json", R"({"error":"cannot delete the last provider"})");
                    return http::FLAG_ERROR;
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
                        return http::FLAG_DONE;
                    }
                }

                output = build_http_response(404, "application/json", R"({"error":"provider not found"})");
                return http::FLAG_ERROR;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_providers_delete", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // —————————————— 供应商 API Key 加密存储 API ——————————————

        // POST /api/provider/key  body: {"id":"deepseek-1","api_key":"sk-xxxx"}
        // 将 api_key 加密后存入 workspace/tokens/providers/<id>.enc
        int handle_provider_key_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
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
                    return http::FLAG_ERROR;
                }

                std::string ws = ws_path();
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
                        return http::FLAG_ERROR;
                    }
                    tool_unit::writeFile(file_path, encrypted);
                    output = build_http_response(200, "application/json",
                                                 json{{"status", "saved"}, {"id", id}}.dump());
                }
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_provider_key_set", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // GET /api/provider/key/:id  → 返回解密后的 {id, api_key}
        int handle_provider_key_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                auto it = params.find("id");
                std::string id = (it != params.end()) ? it->second : "";
                if (id.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing id"})");
                    return http::FLAG_ERROR;
                }

                std::string ws = ws_path();
                std::string file_path = ws + "/tokens/providers/" + id + ".enc";

                if (!std::filesystem::exists(file_path))
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "api_key not found"}, {"id", id}}.dump());
                    return http::FLAG_ERROR;
                }

                std::string encrypted = tool_unit::readFile(file_path);
                if (!encrypted.empty() && encrypted.back() == '\n')
                    encrypted.pop_back();

                std::string api_key = crypto_unit::decrypt(encrypted, crypto_context::key());
                if (api_key.empty())
                {
                    output = build_http_response(500, "application/json", R"({"error":"decryption failed"})");
                    return http::FLAG_ERROR;
                }

                output = build_http_response(200, "application/json",
                                             json{{"id", id}, {"api_key", api_key}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_provider_key_get", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        int handle_channels_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            output = build_http_response(200, "application/json", run_unit::settings["channels"].dump());
            return http::FLAG_DONE;
        }

        // POST /api/channels/add  body: {"name":"Telegram","type":"telegram","status":"active","config":{...}}
        // 频道名是身份（token 文件名也用它），因此名称一经创建不可改。
        int handle_channels_add(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            (void)params;
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);

                const std::string name = trim_str(req.value("name", ""));
                const std::string type = trim_str(req.value("type", ""));
                const std::string status = req.value("status", "inactive") == "active" ? "active" : "inactive";

                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return http::FLAG_ERROR;
                }
                if (!channels::find_driver(type))
                {
                    output = build_http_response(400, "application/json",
                                                 json{{"error", "unknown channel type: " + type}}.dump());
                    return http::FLAG_ERROR;
                }
                if (channel_by_name(name))
                {
                    output = build_http_response(409, "application/json",
                                                 json{{"error", "channel name already exists: " + name}}.dump());
                    return http::FLAG_ERROR;
                }

                json cfg = req.value("config", json::object());
                if (!cfg.is_object())
                    cfg = json::object();

                ensure_channel_array();
                json ch = {{"name", name}, {"type", type}, {"status", status},
                           {"user_count", 1}, {"config", cfg}};
                run_unit::settings["channels"].push_back(ch);
                config::save();
                logx::info("channel", "created channel " + name + " (" + type + ")");

                const bool started = (status == "active") ? channels::start(name) : false;
                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"channel", ch}, {"started", started}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channels_add", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // POST /api/channels/update  body: {"name":"Telegram","status":"active","config":{...}}
        // 只处理类型 / 状态 / 参数；改状态会即时启停 worker。
        int handle_channels_update(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            (void)params;
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);

                const std::string name = trim_str(req.value("name", ""));
                json *ch = channel_by_name(name);
                if (!ch)
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "channel not found: " + name}}.dump());
                    return http::FLAG_ERROR;
                }

                const std::string old_status = ch->value("status", "inactive");
                bool restarted = false;

                if (req.contains("type"))
                {
                    const std::string type = trim_str(req.value("type", ""));
                    if (!channels::find_driver(type))
                    {
                        output = build_http_response(400, "application/json",
                                                     json{{"error", "unknown channel type: " + type}}.dump());
                        return http::FLAG_ERROR;
                    }
                    (*ch)["type"] = type;
                }
                if (req.contains("config") && req["config"].is_object())
                    (*ch)["config"] = req["config"];
                if (req.contains("status"))
                    (*ch)["status"] = (req.value("status", "") == "active") ? "active" : "inactive";

                config::save();

                const std::string new_status = ch->value("status", "inactive");
                if (new_status == "active")
                {
                    if (old_status != "active")
                        restarted = channels::start(name);
                }
                else if (old_status == "active")
                {
                    channels::stop(name);
                }

                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"channel", *ch}, {"restarted", restarted}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channels_update", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // POST /api/channels/delete  body: {"name":"Telegram"}
        int handle_channels_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            (void)params;
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);
                const std::string name = trim_str(req.value("name", ""));

                if (!channel_by_name(name))
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "channel not found: " + name}}.dump());
                    return http::FLAG_ERROR;
                }

                channels::stop(name);

                auto &arr = run_unit::settings["channels"];
                for (auto it = arr.begin(); it != arr.end(); ++it)
                {
                    if (it->value("name", "") == name)
                    {
                        arr.erase(it);
                        break;
                    }
                }
                config::save();
                logx::info("channel", "deleted channel " + name);
                output = build_http_response(200, "application/json", json{{"status", "deleted"}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channels_delete", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // —————————————— 频道 Token 加密存储 API ——————————————

        // POST /api/channel/token  body: {"name":"Telegram","token":"1234:xxxx"}
        // 将 token 加密后存入 workspace/tokens/<name>.enc
        int handle_channel_token_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
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
                    return http::FLAG_ERROR;
                }

                std::string ws = ws_path();
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
                        return http::FLAG_ERROR;
                    }
                    tool_unit::writeFile(file_path, encrypted);
                    output = build_http_response(200, "application/json",
                                                 json{{"status", "saved"}, {"name", name}}.dump());
                }
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_token_set", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // GET /api/channel/token/:name  → 返回解密后的 {name, token}
        // Python 机器人启动时调用此接口获取 bot_token
        int handle_channel_token_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                auto it = params.find("name");
                std::string name = (it != params.end()) ? it->second : "";
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return http::FLAG_ERROR;
                }

                std::string ws = ws_path();
                std::string file_path = ws + "/tokens/" + name + ".enc";

                if (!std::filesystem::exists(file_path))
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "token not found"}, {"name", name}}.dump());
                    return http::FLAG_ERROR;
                }

                std::string encrypted = tool_unit::readFile(file_path);
                if (!encrypted.empty() && encrypted.back() == '\n')
                    encrypted.pop_back();

                std::string token = crypto_unit::decrypt(encrypted, crypto_context::key());
                if (token.empty())
                {
                    output = build_http_response(500, "application/json", R"({"error":"decryption failed"})");
                    return http::FLAG_ERROR;
                }

                output = build_http_response(200, "application/json",
                                             json{{"name", name}, {"token", token}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_token_get", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // GET /api/channel/qr/:name → {name, state, detail, qr_svg(base64), qr_url, running}
        int handle_channel_qr(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                auto it = params.find("name");
                std::string name = (it != params.end()) ? it->second : "";
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return http::FLAG_ERROR;
                }
                output = build_http_response(200, "application/json", bot::channel_qr_json(name).dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_qr", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // GET /api/channel/status → 全部内置 Bot 运行状态
        int handle_channel_status(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                output = build_http_response(200, "application/json", bot::channel_status_json().dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_status", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // POST /api/channel/start  body: {"name":"WeChat"}
        // 运行时按 settings.json channels 配置拉起指定频道的内置 Bot
        int handle_channel_start(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);
                std::string name = req.value("name", "");
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return http::FLAG_ERROR;
                }
                bool started = channels::start(name);
                json out = bot::channel_qr_json(name);
                out["started"] = started;
                output = build_http_response(200, "application/json", out.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_start", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        // POST /api/channel/stop  body: {"name":"WeChat"}
        // 只请求停止：worker 是长轮询，实际退出要等当前请求返回
        int handle_channel_stop(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            (void)params;
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json req = json::parse(body);
                std::string name = req.value("name", "");
                if (name.empty())
                {
                    output = build_http_response(400, "application/json", R"({"error":"missing name"})");
                    return http::FLAG_ERROR;
                }
                const bool stopped = channels::stop(name);
                json out = bot::channel_qr_json(name);
                out["stopped"] = stopped;
                output = build_http_response(200, "application/json", out.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_channel_stop", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        int handle_tools_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            output = build_http_response(200, "application/json", run_unit::tools_list.dump());
            return http::FLAG_DONE;
        }

        int handle_tools_toggle(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
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
                    std::string tools_path = ws_path() + "/tools/tools.json";
                    tool_unit::writeFile(tools_path, run_unit::tools_list.dump(4));
                }
                else
                {
                    output = build_http_response(404, "application/json",
                                                 json{{"error", "tool not found: " + tool_name}}.dump());
                    return http::FLAG_ERROR;
                }

                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"name", tool_name}, {"enabled", enabled}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_tools_toggle", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }

        // ————— 工具权限系统 API —————
        int handle_permissions_get(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                json resp = {
                    {"rules", perm_unit::manager().rules_json()},
                    {"defaults", perm_unit::manager().defaults_json()},
                    {"pending", perm_unit::manager().pending_json()},
                    {"timeout_sec", perm_unit::manager().timeout_sec()}};
                output = build_http_response(200, "application/json", resp.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_permissions_get", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_permissions_set(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string tool = request.value("tool", "");
                std::string pattern = request.value("pattern", "");
                std::string policy = request.value("policy", "");
                std::string err;
                if (!perm_unit::manager().set_rule(tool, pattern, policy, err))
                {
                    output = build_http_response(400, "application/json", json{{"error", err}}.dump());
                    return http::FLAG_ERROR;
                }
                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"tool", tool}, {"pattern", pattern}, {"policy", policy}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_permissions_set", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_permissions_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string tool = request.value("tool", "");
                std::string pattern = request.value("pattern", "");
                std::string err;
                if (!perm_unit::manager().delete_rule(tool, pattern, err))
                {
                    output = build_http_response(404, "application/json", json{{"error", err}}.dump());
                    return http::FLAG_ERROR;
                }
                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"tool", tool}, {"pattern", pattern}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_permissions_delete", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_permission_respond(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string id = request.value("id", "");
                bool granted = request.value("granted", false);
                bool remember = request.value("remember", false);
                std::string err;
                if (!perm_unit::manager().respond(id, granted, remember, err))
                {
                    output = build_http_response(404, "application/json", json{{"error", err}}.dump());
                    return http::FLAG_ERROR;
                }
                output = build_http_response(200, "application/json",
                                             json{{"status", "OK"}, {"id", id}, {"granted", granted}, {"remember", remember}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_permission_respond", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_fs_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request;
                if (!body.empty())
                {
                    try
                    {
                        request = json::parse(body);
                    }
                    catch (...)
                    {
                        request = json::object();
                    }
                }
                if (!request.is_object())
                    request = json::object();
                std::string req_path = request.value("path", "");
                if (req_path.empty())
                {
                    // 兼容 GET /api/fs/list?path=xxx
                    std::string url = http::extract_url(input);
                    size_t q = url.find("path=");
                    if (q != std::string::npos)
                        req_path = url.substr(q + 5);
                }

                std::string ws_abs = ws_path();
                std::string full_path = ws_abs;
                if (!req_path.empty() && req_path != "/")
                {
                    full_path = (std::filesystem::path(ws_abs) / req_path).string();
                }
                full_path = std::filesystem::absolute(full_path).string();

                // 安全检查：不超出工作目录
                if (!paths::is_within(full_path, ws_abs))
                {
                    output = build_http_response(403, "application/json", R"({"error":"Path outside workspace"})");
                    return http::FLAG_ERROR;
                }

                if (!std::filesystem::exists(full_path))
                {
                    output = build_http_response(404, "application/json", R"({"error":"Path not found"})");
                    return http::FLAG_ERROR;
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
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_fs_list", e.what());
                output = build_http_response(500, "application/json", json{{"error", e.what()}}.dump());
                return http::FLAG_ERROR;
            }
        }
        int handle_fs_used(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                auto used = run_unit::get_used_files();
                json arr = json::array();
                for (auto &f : used)
                    arr.push_back(f);
                output = build_http_response(200, "application/json", json{{"files", arr}}.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_fs_used", e.what());
                output = build_http_response(200, "application/json", R"({"files":[]})");
                return http::FLAG_DONE;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_fs_used", "unknown error");
                output = build_http_response(200, "application/json", R"({"files":[]})");
                return http::FLAG_DONE;
            }
        }
        int handle_todos_list(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                json::array_t todos = json::parse(tool_unit::readFile(
                    ws_path() + "/sys/todos.json"));
                output = build_http_response(200, "application/json", json(todos).dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_todos_list", e.what());
                output = build_http_response(500, "application/json", "[]");
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_todos_list", "unknown error");
                output = build_http_response(500, "application/json", "[]");
                return http::FLAG_ERROR;
            }
        }
        int handle_todos_setting(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                std::string id = params.count("id") ? params.at("id") : "unknown";
                json::array_t todos = json::parse(tool_unit::readFile(
                    ws_path() + "/sys/todos.json"));
                for (size_t i = 0; i < todos.size(); ++i)
                {
                    if (todos[i]["id"].get<std::string>() == id)
                    {
                        todos[i] = request;
                        tool_unit::writeFile(ws_path() +
                                                 "/sys/todos.json",
                                             json(todos).dump());
                        output = build_http_response(200, "application/json", "{}");
                        return http::FLAG_DONE;
                    }
                }
                output = build_http_response(404, "application/json", "{}");
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_todos_setting", e.what());
                output = build_http_response(500, "application/json", "{}");
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_todos_setting", "unknown error");
                output = build_http_response(500, "application/json", "{}");
                return http::FLAG_ERROR;
            }
            return 0;
        }
        int handle_todos_delete(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            std::string id = params.count("id") ? params.at("id") : "unknown";
            json::array_t todos = json::parse(tool_unit::readFile(
                ws_path() + "/sys/todos.json"));
            todos.erase(std::remove_if(todos.begin(), todos.end(), [&](const json &t)
                                       { return t["id"].get<std::string>() == id; }),
                        todos.end());
            tool_unit::writeFile(ws_path() + "/sys/todos.json",
                                 json(todos).dump());
            output = build_http_response(200, "application/json", "{}");
            return http::FLAG_DONE;
        }
        int handle_todos_new(std::string &input, std::string &output, const std::map<std::string, std::string> &params)
        {
            std::lock_guard<std::mutex> lk(chat_mutex);
            try
            {
                size_t header_end = input.find("\r\n\r\n");
                std::string body = (header_end != std::string::npos) ? input.substr(header_end + 4) : "";
                json request = json::parse(body);
                json::array_t todos = json::parse(tool_unit::readFile(
                    ws_path() + "/sys/todos.json"));
                todos.push_back(request);
                tool_unit::writeFile(ws_path() +
                                         "/sys/todos.json",
                                     json(todos).dump());
                output = build_http_response(200, "application/json", request.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                webui_log("ERROR", "handle_todos_new", e.what());
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_todos_new", "unknown error");
                return http::FLAG_ERROR;
            }
        }
        // SSE 流式对话：把 agent::run_turn 的事件回调翻译成 SSE 帧
        void handle_input_streaming(std::string &input, http::WriteCallback write,
                                    const std::map<std::string, std::string> &params)
        {
            (void)params;
            auto sse = [&](const json &data)
            {
                write("data: " + data.dump() + "\n\n");
            };

            try
            {
                size_t header_end = input.find("\r\n\r\n");
                if (header_end == std::string::npos)
                {
                    write("HTTP/1.1 400 Bad Request\r\n\r\n");
                    return;
                }
                json request = json::parse(input.substr(header_end + 4));

                write("HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream; charset=utf-8\r\n"
                      "Cache-Control: no-cache\r\n"
                      "Connection: keep-alive\r\n"
                      "Access-Control-Allow-Origin: *\r\n"
                      "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                      "Access-Control-Allow-Headers: Content-Type\r\n"
                      "\r\n");

                agent::Hooks hooks;
                hooks.on_token = [&](const std::string &chunk)
                { sse({{"type", "token"}, {"content", chunk}}); };
                hooks.on_thinking = [&](const std::string &chunk)
                { sse({{"type", "thinking"}, {"content", chunk}}); };
                hooks.on_event = [&](const json &ev)
                { sse(ev); };

                agent::TurnResult result = agent::run_turn(request, hooks);

                json usage = {{"prompt_cost", result.prompt_tokens},
                              {"completion_cost", result.completion_tokens},
                              {"total_cost", result.prompt_tokens + result.completion_tokens}};
                sse({{"type", "done"}, {"usage", usage}, {"rounds", result.rounds}});
            }
            catch (const std::exception &e)
            {
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
                    return http::FLAG_DONE;
                }
                std::string body = input.substr(header_end + 4);
                json request = json::parse(body);
                // 与频道 bot 线程共用同一进程内 Channel 处理逻辑（channel_chat）
                json result = channel_chat(request);
                output = build_http_response(200, "application/json", result.dump());
                return http::FLAG_DONE;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Error in handle_input_packed: " << e.what() << std::endl;
                webui_log("ERROR", "handle_input_packed", e.what());
                json err = {{"error", e.what()}};
                output = build_http_response(500, "application/json", err.dump());
                return http::FLAG_ERROR;
            }
            catch (...)
            {
                webui_log("ERROR", "handle_input_packed", "unknown error");
                output = build_http_response(500, "application/json", R"({"error":"unknown"})");
                return http::FLAG_ERROR;
            }
        }

} // namespace server
