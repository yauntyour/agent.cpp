// ============================================================================
//  core/log.hpp —— 统一日志
//
//  同时写控制台与 <agent_root>/logs/agent.log；日志文件在进程启动时按大小
//  滚动一次（超过 4MB 时改名为 .1），避免长期运行后无限增长。
// ============================================================================
#pragma once
#ifndef AGENTCPP_CORE_LOG_HPP
#define AGENTCPP_CORE_LOG_HPP

#include "core/common.hpp"

namespace logx
{
    // 初始化日志文件（幂等）。path 为空则只写控制台。
    void init(const fs::path &path);

    // 桌面模式关闭控制台输出，日志只落文件
    void set_console(bool enabled);

    // level: DEBUG / INFO / WARN / ERROR
    void write(const std::string &level, const std::string &scope, const std::string &message);

    inline void debug(const std::string &scope, const std::string &message) { write("DEBUG", scope, message); }
    inline void info(const std::string &scope, const std::string &message) { write("INFO", scope, message); }
    inline void warn(const std::string &scope, const std::string &message) { write("WARN", scope, message); }
    inline void error(const std::string &scope, const std::string &message) { write("ERROR", scope, message); }

    // 读取最近的日志（排障用，不再对外暴露 HTTP 接口），max_lines <= 0 表示全部
    std::string tail(int max_lines = 200);
} // namespace logx

#endif // AGENTCPP_CORE_LOG_HPP
