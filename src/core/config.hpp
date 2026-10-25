// ============================================================================
//  core/config.hpp —— 配置加载 / 迁移 / 目录播种
//
//  配置实体仍然是 run_unit::settings（全局单例，工具链、权限、频道、路由都读它），
//  本模块负责：
//    1. 决定配置文件位置（<agent_root>/settings.json，可用 --settings 覆盖）
//    2. 首次运行时用内置模板播种整棵 .agent 目录
//    3. 老版本（settings.json 与 workspace/ 都在 exe 同级）自动迁移
//    4. 补齐新增字段，保证旧配置继续可用
// ============================================================================
#pragma once
#ifndef AGENTCPP_CORE_CONFIG_HPP
#define AGENTCPP_CORE_CONFIG_HPP

#include "core/common.hpp"
#include "core/paths.hpp"
#include "agent/session.hpp"

namespace config
{
    // 内置默认配置（首次运行时写入 settings.json）
    const json &defaults();

    // 读取配置；path 为空时用 <agent_root>/settings.json。
    // 文件不存在则用默认值创建。返回是否新建了文件。
    bool load(const std::string &path = "");

    // 把内存中的 run_unit::settings 写回磁盘
    void save();

    // 当前配置文件路径
    const fs::path &file();

    // ---- 便捷读取 ----------------------------------------------------------
    std::string str(const char *key, const std::string &def = "");

    // 按 "a.b.c" 点号路径读取字符串；任一层缺失或不是字符串时返回 def
    std::string str_path(const std::string &path, const std::string &def = "");
    size_t num(const char *key, size_t def = 0);
    bool flag(const char *key, bool def = false);
    json obj(const char *key);

    std::string user_name();
    std::string agent_name();

    // ---- 目录 --------------------------------------------------------------
    // <agent_root>/<workspace>（配置里的 workspace，可为绝对路径）
    fs::path workspace_dir();

    // WebUI 路径：可能是单文件（webui.html）也可能是目录（webui/）
    fs::path webui_path();
    // 目录模式下使用的首页文件名
    std::string webui_index();

    fs::path logs_dir();
    fs::path sessions_dir();
    fs::path memory_dir();
    fs::path assets_dir();
    fs::path tokens_dir();
    fs::path sys_dir();
    fs::path tools_dir();

    // ---- 初始化 ------------------------------------------------------------
    // 创建整棵 .agent 目录结构，缺失的模板文件用内置内容补齐。
    void ensure_layout();

    // 校验配置完整性（老版本 run_unit::init_check 的替代），失败抛异常
    void validate();

    // 只校验结构，不抛异常（WebUI 保存前调用）
    bool validate_json(const json &j);

    // 深合并后落盘：WebUI 只提交它认识的字段，未提交的（webui / server / root …）
    // 必须保留原值，否则一次保存就会把整段配置抹掉。
    bool apply(const json &incoming, std::string &error);

    // 递归合并（对象合并，数组/标量整体替换）
    void deep_merge(json &dst, const json &src);

    // 老版本布局迁移：<exeDir>/settings.json + <exeDir>/workspace/ → .agent/
    // 返回 true 表示执行了迁移
    bool migrate_legacy();

    // 便捷：把配置里的相对路径解析到 workspace 下
    fs::path in_workspace(const std::string &rel);
} // namespace config

#endif // AGENTCPP_CORE_CONFIG_HPP
