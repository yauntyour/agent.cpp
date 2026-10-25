#include "agent/runtime.hpp"

#include "agent/memory.hpp"
#include "agent/providers.hpp"
#include "agent/tools.hpp"
#include "agent/commands.hpp"
#include "core/log.hpp"
#include "core/text.hpp"

namespace agent
{
    namespace
    {
        std::string g_system_prompt;
        std::string g_tools_list_str;

        std::string replace_all(std::string s, const std::string &from, const std::string &to)
        {
            if (from.empty())
                return s;
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos)
            {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
            return s;
        }

        // 把 tools/tools.json 里的 name + description 拼成一段注入文本
        std::string build_tools_list()
        {
            try
            {
                const fs::path p = config::tools_dir() / "tools.json";
                std::error_code ec;
                if (!fs::exists(p, ec))
                    return {};
                json arr = json::parse(tool_unit::readFile(paths::to_utf8(p)));
                if (!arr.is_array() || arr.empty())
                    return {};
                std::string out = "## Available tools\n";
                bool any = false;
                for (auto &t : arr)
                {
                    if (!t.value("enabled", true))
                        continue;
                    std::string name = t.value("name", "");
                    if (name.empty())
                        continue;
                    any = true;
                    out += "- " + name + ": " + t.value("description", "");
                    out += "\n";
                }
                return any ? out : std::string();
            }
            catch (const std::exception &e)
            {
                logx::warn("prompt", std::string("tools.json: ") + e.what());
                return {};
            }
        }

        // 记账：把本轮 token 用量累加到 data.json
        void record_usage(const std::string &session_id, size_t prompt_tokens, size_t completion_tokens)
        {
            if (session_id.empty())
                return;
            json &data = run_unit::agent_data_manager.data;
            if (!data.is_object())
                data = json::object();
            json &usages = data["usages"];
            if (!usages.is_object())
                usages = json::object();
            json &entry = usages[session_id];
            if (!entry.is_object())
                entry = json::object();
            entry["prompt_cost"] = entry.value("prompt_cost", 0) + prompt_tokens;
            entry["completion_cost"] = entry.value("completion_cost", 0) + completion_tokens;
            entry["total_cost"] = entry.value("total_cost", 0) + prompt_tokens + completion_tokens;
        }
    } // namespace

    std::mutex chat_mutex;

    const std::string &system_prompt() { return g_system_prompt; }
    const std::string &tools_list_str() { return g_tools_list_str; }

    void load_prompts()
    {
        const fs::path prompt_file = config::in_workspace(config::str("prompt", "agent.txt"));
        g_system_prompt = tool_unit::readFile(paths::to_utf8(prompt_file)) + run_unit::cs_prompt;
        // 去掉缩进与 CRLF，避免模型把排版空白当成语义
        g_system_prompt = replace_all(g_system_prompt, "    ", "");
        g_system_prompt = replace_all(g_system_prompt, "\r\n", "");
        g_tools_list_str = build_tools_list();
    }

    // ==================================================================
    //  一轮对话
    // ==================================================================
    TurnResult run_turn(const json &request, const Hooks &hooks)
    {
        std::lock_guard<std::mutex> lk(chat_mutex);

        if (!request.contains("messages") || !request["messages"].is_string())
            throw std::runtime_error("missing 'messages'");
        const std::string user_message = request["messages"].get<std::string>();

        std::string model = request.value("model", "default");
        if (model.empty() || model == "default")
            model = config::str("model", "default");

        const bool think_mode = request.value("think", false);
        const std::string channel = request.value("channel", "");
        const std::string sid = request.value("session_id", "");

        // ---- 选择会话 -------------------------------------------------
        auto &sessions = run_unit::agent_session_manager;
        std::shared_ptr<run_unit::SessionContext> session;
        if (!channel.empty())
        {
            session = sessions.get(channel);
            if (!session)
            {
                session = sessions.create();
                sessions.change_session(session->session_id);
            }
        }
        else if (!sid.empty())
        {
            session = sessions.get(sid);
            if (!session)
                throw std::runtime_error("session not found");
            sessions.change_session(sid);
        }
        else
        {
            session = sessions.get_current();
        }
        if (!session)
            session = sessions.create();

        // ---- 构建上下文 -----------------------------------------------
        // 只允许一条 system 消息，且必须位于最前面：
        // llama.cpp / 部分严格实现会在第二条 system 消息处直接报错，
        // 因此把工具清单与记忆摘要合并进同一条系统提示。
        std::string system_text = g_system_prompt;
        if (!g_tools_list_str.empty())
            system_text += "\n\n" + g_tools_list_str;
        if (!session->is_memory_empty())
            system_text += "\n\n## Memory of previous conversations\n" +
                           session->memory["abstracts"].get<std::string>();

        json context = json::array();
        context.push_back({{"role", "system"}, {"content", system_text}});

        if (session->is_memory_empty())
            for (auto &msg : session->messages)
                context.push_back(msg);

        json contents = json::array();
        if (!channel.empty())
            contents.push_back({{"type", "text"},
                                {"text", "Messages received from " + channel + ":" + user_message}});
        else
            contents.push_back({{"type", "text"}, {"text", user_message}});

        if (request.contains("images") && request["images"].is_array())
            for (auto &img : request["images"])
                contents.push_back({{"type", "image_url"}, {"image_url", {{"url", img.get<std::string>()}}}});

        json user_msg = {{"role", config::user_name()}, {"content", contents}};
        context.push_back(user_msg);
        session->messages.push_back(user_msg);

        // ---- 工具循环 -------------------------------------------------
        TurnResult result;
        const size_t max_rounds = config::num("max_mpc_rounds", 5);
        const bool use_stream = config::flag("stream", true);
        const std::string ctx_id = !channel.empty() ? channel : session->session_id;

        for (size_t round = 0; round < max_rounds; ++round)
        {
            json llm_req = {
                {"model", model},
                {"messages", context},
                {"think", think_mode}};

            std::string response_text;
            std::string thinking_text;

            if (use_stream)
            {
                response_text = client_stream_generate(
                    llm_req,
                    [&](const std::string &chunk)
                    {
                        if (hooks.on_token)
                            hooks.on_token(chunk);
                    },
                    [&](const std::string &chunk)
                    {
                        thinking_text += chunk;
                        if (hooks.on_thinking)
                            hooks.on_thinking(chunk);
                    });
                // 流式接口通常不回传 usage，按字符数粗估（含推理内容）
                result.prompt_tokens += llm_req["messages"].dump().size() / 4;
                result.completion_tokens += (response_text.size() + thinking_text.size()) / 4;
            }
            else
            {
                json resp;
                if (!client_generate(llm_req, resp))
                    throw std::runtime_error("LLM generate failed");
                if (!resp.contains("choices") || resp["choices"].empty())
                    throw std::runtime_error("LLM returned no choices");
                const json &msg = resp["choices"][0]["message"];
                if (think_mode && msg.contains("reasoning_content") &&
                    msg["reasoning_content"].is_string())
                    thinking_text = msg["reasoning_content"].get<std::string>();
                response_text = msg.value("content", "");
                if (resp.contains("usage"))
                {
                    const json &u = resp["usage"];
                    if (u.contains("prompt_tokens"))
                        result.prompt_tokens += u["prompt_tokens"].get<size_t>();
                    if (u.contains("completion_tokens"))
                        result.completion_tokens += u["completion_tokens"].get<size_t>();
                }
            }

            if (think_mode && !thinking_text.empty())
                result.thinking.push_back(thinking_text);

            json agent_msg = {{"role", config::agent_name()}, {"content", response_text}};
            context.push_back(agent_msg);
            session->messages.push_back(agent_msg);
            result.content += response_text;
            result.rounds = round + 1;

            // ---- 扫描 <tool> / <cs> 标签 ------------------------------
            perm_unit::NotifyCallback notify;
            if (hooks.on_event)
            {
                notify = [&hooks](const perm_unit::PendingRequest &req, bool finished)
                {
                    if (!finished)
                        hooks.on_event({{"type", "permission_request"},
                                        {"id", req.id},
                                        {"session_id", req.session_id},
                                        {"tool", req.tool},
                                        {"args", req.args},
                                        {"timeout", req.timeout_sec}});
                    else
                        hooks.on_event({{"type", "permission_result"},
                                        {"id", req.id},
                                        {"granted", req.granted},
                                        {"remember", req.remember},
                                        {"timed_out", req.timed_out}});
                };
            }

            std::string sys_out;
            auto [tools_called, tools_ok] = tool_unit::tools_scan(response_text, sys_out, ctx_id, notify);
            auto cs_called = cs_unit::cs_scan(response_text, sys_out);
            (void)tools_ok;

            // ---- 记录每次工具调用（前端展示 / 频道回显） --------------
            auto tool_tags = extractAllTags(response_text, "tool");
            auto sys_out_blocks = extractAllTags(sys_out, "system_output");
            for (size_t i = 0; i < tool_tags.size(); ++i)
            {
                auto [name, args] = parseArgs(tool_tags[i]);
                json entry = {
                    {"name", std::string(name)},
                    {"args", std::string(args)},
                    {"round", round}};
                if (i < sys_out_blocks.size())
                    entry["output"] = std::string(sys_out_blocks[i]);
                result.tools.push_back(entry);

                if (hooks.on_event)
                {
                    hooks.on_event({{"type", "tool_start"},
                                    {"name", std::string(name)},
                                    {"args", std::string(args)},
                                    {"round", round}});
                    if (i < sys_out_blocks.size())
                        hooks.on_event({{"type", "tool_output"},
                                        {"content", std::string(sys_out_blocks[i])},
                                        {"round", round}});
                    hooks.on_event({{"type", "tool_end"}, {"name", std::string(name)}, {"round", round}});
                }
            }

            if (tools_called == 0 && cs_called == 0)
                break;

            // ---- 工具结果回灌上下文 ----------------------------------
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
            session->messages.push_back(sys_msg);
        }

        // ---- 收尾：自动记忆 + 记账 -----------------------------------
        maybe_auto_summarize(session, model);
        record_usage(session->session_id, result.prompt_tokens, result.completion_tokens);

        return result;
    }

    json channel_chat(const json &request)
    {
        TurnResult r = run_turn(request);
        return json{
            {"content", r.content},
            {"thinking", r.thinking},
            {"tools", r.tools},
            {"rounds", r.rounds},
            {"usage", {{"prompt_cost", r.prompt_tokens},
                       {"completion_cost", r.completion_tokens},
                       {"total_cost", r.prompt_tokens + r.completion_tokens}}}};
    }
} // namespace agent
