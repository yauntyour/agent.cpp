#include "agent/memory.hpp"

#include "core/config.hpp"
#include "core/log.hpp"
#include "agent/providers.hpp"

namespace agent
{
    std::string extract_text(const json &msg)
    {
        if (!msg.is_object() || !msg.contains("content"))
            return {};
        const json &c = msg["content"];
        if (c.is_string())
            return c.get<std::string>();
        if (c.is_array())
        {
            std::string text;
            for (auto &part : c)
                if (part.is_object() && part.value("type", "") == "text")
                    text += part.value("text", "");
            return text;
        }
        return {};
    }

    int save_memory(std::shared_ptr<run_unit::SessionContext> session, const std::string &model)
    {
        if (!session)
            return -1;
        try
        {
            size_t prompt_tokens = 0;
            size_t completion_tokens = 0;

            json req = {
                {"model", model},
                {"messages", json::array({{{"role", "system"}, {"content", session->summary_query()}}})},
                {"stream", false}};

            json resp;
            if (!client_generate(req, resp))
            {
                logx::warn("memory", "summarize request failed");
                return -1;
            }
            if (resp.contains("usage"))
            {
                auto &u = resp["usage"];
                if (u.contains("prompt_tokens"))
                    prompt_tokens += u["prompt_tokens"].get<size_t>();
                if (u.contains("completion_tokens"))
                    completion_tokens += u["completion_tokens"].get<size_t>();
            }
            if (!resp.contains("choices") || resp["choices"].empty())
                return -1;

            std::string content = resp["choices"][0]["message"].value("content", "");
            try
            {
                auto parsed = json::parse(content);
                if (parsed.contains("abstracts") && parsed["abstracts"].is_string())
                    session->memory["abstracts"] = parsed["abstracts"].get<std::string>();
                if (parsed.contains("keywords") && parsed["keywords"].is_string())
                    session->memory["keywords"] = parsed["keywords"].get<std::string>();
            }
            catch (...)
            {
                // 模型没按 JSON 回复时，整段当摘要
                session->memory["abstracts"] = content;
            }

            session->memory["created_at"] = std::to_string(std::time(nullptr));
            session->last_saved_index = session->messages.size();

            json &data = run_unit::agent_data_manager.data;
            if (!data.is_object())
                data = json::object();
            if (!data["usages"].is_object())
                data["usages"] = json::object();
            json &mem_usage = data["usages"]["memory"];
            if (!mem_usage.is_object())
                mem_usage = json::object();
            mem_usage["prompt_cost"] = mem_usage.value("prompt_cost", 0) + prompt_tokens;
            mem_usage["completion_cost"] = mem_usage.value("completion_cost", 0) + completion_tokens;
            mem_usage["total_cost"] = mem_usage.value("total_cost", 0) + prompt_tokens + completion_tokens;

            logx::info("memory", "memory updated for session " + session->session_id);
            return 0;
        }
        catch (const std::exception &e)
        {
            logx::error("memory", e.what());
            return -1;
        }
    }

    void maybe_auto_summarize(std::shared_ptr<run_unit::SessionContext> session, const std::string &model)
    {
        if (!session)
            return;
        size_t ctx_size = session->messages.dump().size();
        size_t max_ctx = config::num("max_context", 1048576);
        if (max_ctx > 0 && ctx_size > max_ctx)
        {
            logx::info("memory", "auto summarize: context " + std::to_string(ctx_size) +
                                     " > max " + std::to_string(max_ctx));
            save_memory(session, model);
        }
    }
} // namespace agent
