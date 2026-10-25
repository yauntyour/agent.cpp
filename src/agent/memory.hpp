// ============================================================================
//  agent/memory.hpp —— 会话记忆摘要
//
//  增量策略：只把「上次保存之后的新消息」连同旧摘要一起交给模型，
//  一次调用同时产出 abstracts 与 keywords，省掉一半 token。
// ============================================================================
#pragma once
#ifndef AGENTCPP_AGENT_MEMORY_HPP
#define AGENTCPP_AGENT_MEMORY_HPP

#include "core/common.hpp"
#include "agent/session.hpp"

namespace agent
{
    // 提取消息里的纯文本（用于统计长度 / 摘要）
    std::string extract_text(const json &msg);

    // 生成或增量更新会话记忆；返回 0 表示成功
    int save_memory(std::shared_ptr<run_unit::SessionContext> session, const std::string &model);

    // 上下文超过 max_context 时自动触发
    void maybe_auto_summarize(std::shared_ptr<run_unit::SessionContext> session, const std::string &model);
} // namespace agent

#endif // AGENTCPP_AGENT_MEMORY_HPP
