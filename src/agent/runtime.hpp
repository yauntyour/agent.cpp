// ============================================================================
//  agent/runtime.hpp —— Agent 主循环
//
//  一条用户消息进来之后发生的事，全部收敛在这里：
//
//      构建上下文 → 调 LLM（流式/非流式）→ 扫描 <tool> / <cs> 标签
//        → 执行工具（含权限审批）→ 把结果回灌上下文 → 下一轮
//        → 没有工具调用则结束 → 触发记忆摘要 / 记账
//
//  WebUI（SSE）与频道（Telegram / WeChat / QQ）共用同一份实现，
//  区别只在 hooks：WebUI 关心 token 与 permission 事件，频道只关心最终文本。
// ============================================================================
#pragma once
#ifndef AGENTCPP_AGENT_RUNTIME_HPP
#define AGENTCPP_AGENT_RUNTIME_HPP

#include "core/common.hpp"
#include "core/config.hpp"
#include "agent/permissions.hpp"
#include "agent/session.hpp"

namespace agent
{
    // 全局聊天锁：频道线程与 HTTP 会话共用同一套 LLM 客户端 / 会话管理器 /
    // 工具队列，所有读写都必须串行化。
    extern std::mutex chat_mutex;

    // 系统提示词（agent.txt + sys/cs.txt，已做空白压缩）
    const std::string &system_prompt();
    // 工具清单文本（注入上下文）
    const std::string &tools_list_str();
    // 启动时加载提示词与工具清单
    void load_prompts();

    // 一轮对话的事件钩子；全部可选
    struct Hooks
    {
        std::function<void(const std::string &)> on_token;    // 正文增量
        std::function<void(const std::string &)> on_thinking; // 推理增量
        std::function<void(const json &)> on_event;           // tool_* / permission_*
    };

    struct TurnResult
    {
        std::string content;
        json thinking = json::array();
        json tools = json::array();
        size_t rounds = 0;
        size_t prompt_tokens = 0;
        size_t completion_tokens = 0;
    };

    // 执行一轮完整对话。request 字段：
    //   messages    : string  必填，用户输入
    //   images      : array<string> 可选，data:image/...;base64,...
    //   model       : string  可选，"default" 表示用配置里的 model
    //   think       : bool    可选
    //   session_id  : string  可选
    //   channel     : string  可选，频道名（会切换到该频道的专用会话）
    TurnResult run_turn(const json &request, const Hooks &hooks = {});

    // 频道用：返回与旧版 /api/input 一致的 JSON
    json channel_chat(const json &request);
} // namespace agent

#endif // AGENTCPP_AGENT_RUNTIME_HPP
