// ============================================================================
//  core/paths.hpp —— 运行时目录布局
//
//  设计参照 cell.cpp：所有运行期数据都收敛到可执行文件旁边的 .agent 目录，
//  因此无论从哪个工作目录启动，数据都落在同一个地方，也不会污染用户目录。
//
//      <exeDir>/.agent/                 数据根（agent_root）
//      ├── settings.json                主配置
//      ├── webui/                       WebUI 静态资源目录（或 webui.html 单文件）
//      ├── logs/agent.log               运行日志
//      └── workspace/                   Agent 工作区
//          ├── agent.txt                系统提示词
//          ├── sessions/ memorys/ ...
//          ├── sys/                     内置指令与统计
//          └── tools/                   自定义 Python 工具
//
//  数据根与工作区都可以在配置里改写（见 core/config.hpp）：
//      "root":      "/data/agent"      → 覆盖 .agent 数据根
//      "workspace": "workspace"        → 相对 root 解析
// ============================================================================
#pragma once
#ifndef AGENTCPP_CORE_PATHS_HPP
#define AGENTCPP_CORE_PATHS_HPP

#include "core/common.hpp"

namespace paths
{
    // 可执行文件所在目录（不带结尾分隔符）。取不到时退化为当前工作目录。
    const fs::path &exe_dir();

    // 数据根目录，默认 <exeDir>/.agent。必须在 config::load() 之前调用 init()。
    const fs::path &agent_root();

    // 覆盖数据根目录（--root 参数或配置里的 root）。传空则回到默认值。
    void set_agent_root(const fs::path &root);

    // 把相对路径按数据根解析；绝对路径原样返回。
    fs::path resolve(const std::string &p);
    fs::path resolve(const fs::path &p);

    // <agent_root>/settings.json
    fs::path settings_file();

    // 判断 child 是否位于 parent 之内（用于静态资源的路径穿越防护）
    bool is_within(const fs::path &child, const fs::path &parent);

    // 跨平台的 UTF-8 路径转换（Windows 上 std::filesystem::path 是 wchar_t）
    std::string to_utf8(const fs::path &p);
    fs::path from_utf8(const std::string &s);
} // namespace paths

#endif // AGENTCPP_CORE_PATHS_HPP
