// ============================================================================
//  desktop/webview2.hpp —— 桌面外壳（Windows + WebView2）
//
//  把 WebUI 装进一个无边框原生窗口：双击 exe 就是桌面应用，既没有浏览器
//  标签页，也没有控制台窗口。
//
//  实现要点：
//    - WebView2 SDK 头（WebView2.h / WebView2EnvironmentOptions.h）随仓库放在
//      third_party/webview2/include，编译期只依赖头文件；
//    - 运行时通过 LoadLibrary 动态解析 WebView2Loader.dll 的
//      CreateCoreWebView2EnvironmentWithOptions，因此不需要导入库，
//      MinGW / MSVC 都能链；
//    - 窗口无系统标题栏，标题栏与最小化/最大化/关闭按钮画在页面里，
//      通过 window.chrome.webview.postMessage 回传命令（见 src/desktop/README 约定）；
//    - 用户数据目录放在 <agent_root>/webview2，缓存不落系统盘；
//    - 找不到 WebView2 运行时时返回 false，调用方退化为打开系统浏览器。
//
//  非 Windows 平台：available() 恒为 false。
// ============================================================================
#pragma once
#ifndef AGENTCPP_DESKTOP_WEBVIEW2_HPP
#define AGENTCPP_DESKTOP_WEBVIEW2_HPP

#include "core/common.hpp"

namespace desktop
{
    struct WindowOptions
    {
        std::string title = "agent.cpp";
        std::string url = "http://127.0.0.1:8080";
        int width = 1280;
        int height = 860;
        int min_width = 960;
        int min_height = 640;
        bool resizable = true;
        bool devtools = false;
        // 系统托盘：常驻图标 + 关闭窗口时最小化到托盘（右键菜单可真正退出）
        bool tray = true;
        bool close_to_tray = true;
        std::string tray_tooltip = "agent.cpp";
    };

    // WebView2 运行时是否可用（DLL 能否加载）
    bool available();

    // 打开窗口并进入消息循环，阻塞直到窗口关闭。
    // 返回 0 表示正常关闭；-1 表示环境不可用；其他值为错误码。
    int run(const WindowOptions &opts);

    // 用系统默认浏览器打开 URL（WebView2 不可用时的退路）
    bool open_in_browser(const std::string &url);

    // ---- 启动期辅助（Windows 实现，其他平台为空操作） ----------------------

    // 隐藏控制台窗口；若当前控制台是终端（cmd/PowerShell）则保持不变，
    // 这样命令行下仍能看到输出。
    void hide_console_if_standalone();

    // 抢占单实例锁。返回 false 表示已有实例在跑（此时可调用 activate_existing）。
    bool single_instance_acquire();
    void single_instance_release();

    // 把已有实例的窗口拉到前台
    bool activate_existing();

    // 启动失败时的可见提示（桌面模式下没有控制台，不能只用 stderr）
    void fatal_dialog(const std::string &title, const std::string &text);
} // namespace desktop

#endif // AGENTCPP_DESKTOP_WEBVIEW2_HPP
