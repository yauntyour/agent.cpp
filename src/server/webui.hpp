// ============================================================================
//  server/webui.hpp —— WebUI 静态资源服务（单文件 / 目录双模式）
//
//  配置项（settings.json → webui）：
//      "path":    "webui"          相对 .agent 的路径；也可以直接写 "webui.html"
//      "index":   "index.html"     目录模式下的首页
//      "password": ""              登录密码（SHA3-256 十六进制，空表示不校验）
//
//  行为：
//    - path 指向文件   → 所有 GET / 都返回该文件（兼容旧的单文件 WebUI）
//    - path 指向目录   → /            → index
//                        /app.css     → <dir>/app.css
//                        /assets/a.js → <dir>/assets/a.js
//      并对 .. 穿越、目录列表做防护；文本资源带 charset，静态资源带 ETag。
// ============================================================================
#pragma once
#ifndef AGENTCPP_SERVER_WEBUI_HPP
#define AGENTCPP_SERVER_WEBUI_HPP

#include "core/common.hpp"
#include "core/config.hpp"

namespace webui
{
    // 根据配置判断当前是目录模式还是单文件模式
    bool is_directory_mode();

    // 解析一个 URL 路径到磁盘文件；返回空路径表示不允许/不存在
    fs::path resolve_request_path(const std::string &url_path);

    // 生成完整 HTTP 响应（供路由注册使用）
    std::string serve(const std::string &raw_request);

    // 只从内嵌资源（webui_assets.hpp）取，供 serve() 与调试接口复用
    std::string serve_embedded(const std::string &path);

    // 处理函数：符合 http::Handler 签名
    int handle(std::string &input, std::string &output,
               const std::map<std::string, std::string> &params);

    // 扩展名 → MIME
    std::string mime_of(const fs::path &p);

    // 运行时诊断信息（/api/webui/info）
    json info();
} // namespace webui

#endif // AGENTCPP_SERVER_WEBUI_HPP
