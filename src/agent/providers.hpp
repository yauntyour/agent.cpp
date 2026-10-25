// ============================================================================
//  agent/providers.hpp —— 模型供应商（OpenAI 兼容 / Ollama / llama.cpp）
//
//  三家的客户端都在 net/llm.hpp 里，本模块只做“当前供应商”的切换与转发，
//  让上层（HTTP 路由、频道、记忆摘要）不必关心具体是哪一个。
// ============================================================================
#pragma once
#ifndef AGENTCPP_AGENT_PROVIDERS_HPP
#define AGENTCPP_AGENT_PROVIDERS_HPP

#include "core/common.hpp"
#include "core/config.hpp"
#include "net/llm.hpp"

namespace agent
{
    enum class ProviderType
    {
        OpenAI,
        Ollama,
        Llama
    };

    ProviderType current_provider_type();
    std::string provider_to_string();

    // 列出当前供应商可用模型（原始 JSON 文本）
    std::string client_models();

    // 非流式生成；返回 false 表示请求失败
    bool client_generate(json &req, json &resp);

    // 流式生成；返回累计的完整回复文本
    std::string client_stream_generate(json &req,
                                       std::function<void(const std::string &)> on_token,
                                       std::function<void(const std::string &)> on_thinking = nullptr);

    // 直接按类型切换
    void switch_provider(const std::string &provider_type, const std::string &base_url,
                         const std::string &api_key);

    // 按 providers[].id 切换（会持久化 current_provider）
    bool switch_provider_by_id(const std::string &provider_id);

    // 查找 providers 数组中的条目；找不到返回 nullptr
    json *find_provider(const std::string &provider_id);

    // 应用启动时按配置初始化当前供应商
    void init_from_settings();

    // 字节数组转十六进制小写串
    std::string hex(const std::uint8_t *data, size_t len);
} // namespace agent

#endif // AGENTCPP_AGENT_PROVIDERS_HPP
