// ============================================================================
//  channels/channel.hpp —— 频道（IM 平台）驱动注册表
//
//  一个频道驱动 = 一个长期运行的 worker 函数：
//      void run(const json &cfg, const std::string &name);
//  worker 自己负责收消息、调 agent::channel_chat、把回复发回去。
//
//  接入新平台只需三步：
//      1. 写一个 xxx.hpp（worker 实现）
//      2. 在 manager.cpp 里 register_driver({"xxx", {"别名"}, "显示名", &xxx_worker})
//      3. settings.json 的 channels 里加一条 {"name": "...", "type": "xxx"}
//
//  内置驱动：telegram / wechat / qq（OneBot v11）
// ============================================================================
#pragma once
#ifndef AGENTCPP_CHANNELS_CHANNEL_HPP
#define AGENTCPP_CHANNELS_CHANNEL_HPP

#include "core/common.hpp"

namespace channels
{
    using Worker = std::function<void(const json &cfg, const std::string &name)>;

    struct Driver
    {
        std::string type;                  // 配置里的 "type"
        std::vector<std::string> aliases;  // 兼容老配置里的 path 提示
        std::string display;               // 人类可读名称
        Worker run;                        // 阻塞式 worker
        std::string hint;                  // 配置说明（显示在 WebUI）
    };

    // 注册/查询
    void register_driver(Driver d);
    const Driver *find_driver(const std::string &type_or_hint);
    std::vector<std::string> driver_types();

    // 从 settings.channels 里按名字拉起单个频道（已运行则直接返回 true）
    bool start(const std::string &name);
    // 请求停止：worker 在下一轮循环检查标志后退出（长轮询可能要等几十秒）
    bool stop(const std::string &name);
    // 拉起所有 status == "active" 的频道
    void start_all();
    // 记录运行状态
    void set_status(const std::string &name, bool running, const std::string &state,
                    const std::string &detail = "", const std::string &error = "");
    json status_json();
    json qr_json(const std::string &name);

    // 频道的 token 持久化（<workspace>/tokens/<name>.enc）
    std::string load_token(const std::string &name);
    bool save_token(const std::string &name, const std::string &token);

    // 内置驱动注册（幂等）
    void register_builtin();
} // namespace channels

#endif // AGENTCPP_CHANNELS_CHANNEL_HPP
