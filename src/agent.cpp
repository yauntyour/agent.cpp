// agent.cpp — 陪伴型智能体（Cosplay Agent）的桌面外壳入口
// =============================================================================
//   agent.exe                    桌面应用（自动在空闲端口上起服务）
//   agent.exe --serve [PORT]     桌面应用 + 固定服务端口（默认 8899）
//   agent.exe --headless [PORT]  只跑服务，不开窗口（调试 / 脚本驱动）
//   agent.exe --ui FILE          从磁盘读页面（改完刷新，不用重编译）
//   agent.exe --smoke            冒烟：窗口 → 页面 → 第一条 state 命令
//   agent.exe --smoke-engine     冒烟：再加一次完整回合（无模型时的错误路径）
//   agent.exe --debug            开发模式：空数据根时自动生成一个测试角色
//
// 默认是「空角色」：第一次打开没有角色卡，用户自己建一个；
// --debug（以及两个冒烟模式）会在空数据根里生成一个测试角色，方便调界面。
//
// 结构与 cell-app 一致：
//   cell.cpp（单文件智能体内核，main 改名后整份编进来）
//   agent-core.hpp   角色内核 + 命令总线 + 事件总线
//   agent-voice.hpp  TTS / ASR / 原生录音
//   agent-im.hpp     IM 接入：Telegram / QQ(OneBot) / 企业微信 / HTTP 桥
//   agent-http.hpp   HTTP + SSE + 音频/头像静态资源
//   agent-ui.hpp     内嵌的界面（build_ui.py 生成）
//
// 线程：每个角色一个回合工作线程；命令在 HTTP 工作线程上串行执行；所有事件
// 通过 event_bus 广播，SSE 订阅者各自排队，慢客户端不会阻塞引擎。
#define main original_main
#include "cell.cpp/cell.cpp"
#undef main

#include "webview/webview.h"

#include "agent-ui.hpp"
#include "agent-core.hpp"
#include "agent-http.hpp"

#include <csignal>
#include <cstdlib>
#ifdef _WIN32
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace app
{
#ifdef _WIN32
    // =====================================================================
    //  托盘：关窗 = 收进通知区，回合、服务、主动关怀都继续跑。
    //  webview.h 在 WM_CLOSE 时销毁窗口（进程随之结束），所以换掉窗口过程，
    //  只把窗口藏起来；图标挂在自己的隐藏窗口上，不依赖 WebView2 窗口。
    // =====================================================================
    namespace tray
    {
        constexpr UINT kMsg = WM_APP + 17;
        constexpr UINT kShow = 1;
        constexpr UINT kData = 2;
        constexpr UINT kQuit = 3;
        constexpr UINT kIconId = 1;

        static HWND g_main = nullptr;
        static WNDPROC g_main_proc = nullptr;
        static HWND g_msg = nullptr;
        static HICON g_icon = nullptr;
        static UINT g_taskbar_created = 0;
        static bool g_quitting = false;
        static bool g_balloon = false;

        static NOTIFYICONDATAW icon_data()
        {
            NOTIFYICONDATAW nid{};
            nid.cbSize = sizeof(nid);
            nid.hWnd = g_msg;
            nid.uID = kIconId;
            return nid;
        }
        static bool add_icon()
        {
            if (!g_msg)
                return false;
            NOTIFYICONDATAW nid = icon_data();
            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
            nid.uCallbackMessage = kMsg;
            nid.hIcon = g_icon;
            wcsncpy(nid.szTip, L"agent", sizeof(nid.szTip) / sizeof(nid.szTip[0]) - 1);
            return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
        }
        static void remove_icon()
        {
            if (!g_msg)
                return;
            NOTIFYICONDATAW nid = icon_data();
            Shell_NotifyIconW(NIM_DELETE, &nid);
        }
        static void restore()
        {
            if (!g_main || !IsWindow(g_main))
                return;
            ShowWindow(g_main, IsIconic(g_main) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(g_main);
            SwitchToThisWindow(g_main, TRUE);
        }
        static void quit()
        {
            g_quitting = true;
            remove_icon();
            if (g_main && IsWindow(g_main))
                DestroyWindow(g_main); // WM_DESTROY 才会结束 webview 的消息循环
        }
        static void balloon_once()
        {
            if (g_balloon || !g_msg)
                return;
            g_balloon = true;
            NOTIFYICONDATAW nid = icon_data();
            nid.uFlags = NIF_INFO;
            nid.dwInfoFlags = NIIF_INFO;
            wcsncpy(nid.szInfoTitle, L"agent", sizeof(nid.szInfoTitle) / sizeof(nid.szInfoTitle[0]) - 1);
            wcsncpy(nid.szInfo, L"窗口已关闭，助手还在后台；单击托盘图标可以回来。",
                    sizeof(nid.szInfo) / sizeof(nid.szInfo[0]) - 1);
            Shell_NotifyIconW(NIM_MODIFY, &nid);
        }
        static void show_menu(HWND owner)
        {
            HMENU m = CreatePopupMenu();
            if (!m)
                return;
            AppendMenuW(m, MF_STRING, kShow, L"显示主窗口");
            AppendMenuW(m, MF_STRING, kData, L"打开数据目录");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, kQuit, L"退出");
            SetMenuDefaultItem(m, kShow, FALSE);
            POINT pt{};
            GetCursorPos(&pt);
            SetForegroundWindow(owner);
            TrackPopupMenuEx(m, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, owner, nullptr);
            PostMessageW(owner, WM_NULL, 0, 0);
            DestroyMenu(m);
        }
        static void open_data_dir()
        {
            std::string cmd = "explorer.exe \"" + cell::root.string() + "\"";
            std::string out, err;
            int code = -1;
            cell::plat::spawn_cmd(cmd, 10, out, code, err);
        }
        static LRESULT CALLBACK msg_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == kMsg)
            {
                switch (LOWORD(lp))
                {
                case WM_LBUTTONUP:
                case WM_LBUTTONDBLCLK:
                    restore();
                    break;
                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    show_menu(hwnd);
                    break;
                }
                return 0;
            }
            if (g_taskbar_created && msg == g_taskbar_created)
            {
                add_icon();
                return 0;
            }
            if (msg == WM_COMMAND)
            {
                switch (LOWORD(wp))
                {
                case kShow:
                    restore();
                    return 0;
                case kData:
                    open_data_dir();
                    return 0;
                case kQuit:
                    quit();
                    return 0;
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        static LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            if (msg == WM_CLOSE && !g_quitting)
            {
                ShowWindow(hwnd, SW_HIDE);
                balloon_once();
                return 0;
            }
            return CallWindowProcW(g_main_proc, hwnd, msg, wp, lp);
        }
        static bool attach(HWND main)
        {
            if (!main || g_main)
                return false;
            HINSTANCE inst = GetModuleHandleW(nullptr);
            g_icon = reinterpret_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                       GetSystemMetrics(SM_CXSMICON),
                                                       GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
            if (!g_icon)
                g_icon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));

            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = inst;
            wc.lpszClassName = L"agent_tray";
            wc.lpfnWndProc = msg_proc;
            RegisterClassExW(&wc);
            g_msg = CreateWindowExW(WS_EX_TOOLWINDOW, L"agent_tray", L"", WS_POPUP,
                                    0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
            if (!g_msg)
                return false;
            g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
            if (!add_icon())
            {
                DestroyWindow(g_msg);
                g_msg = nullptr;
                return false;
            }
            g_main_proc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(main, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(main_proc)));
            if (!g_main_proc)
            {
                DestroyWindow(g_msg);
                g_msg = nullptr;
                return false;
            }
            g_main = main;
            return true;
        }
        static void detach()
        {
            g_quitting = true;
            remove_icon();
            if (g_msg)
            {
                DestroyWindow(g_msg);
                g_msg = nullptr;
            }
        }
    } // namespace tray
#endif

    static void print_usage()
    {
        printf("agent — 陪伴型智能体（Cosplay Agent）\n\n");
        printf("usage: agent.exe [options]\n\n");
        printf("  --serve [PORT]    同时在本机暴露界面与 API（默认 8899）\n");
        printf("  --headless [PORT] 只起服务，不开窗口\n");
        printf("  --ui FILE         从磁盘读界面（开发模式：改完刷新，不重编译）\n");
        printf("  --debug           开发模式：空数据根时自动生成一个测试角色\n");
        printf("  --help            显示帮助\n\n");
        printf("冒烟测试（独立数据根，自动退出）：\n");
        printf("  --smoke           窗口 → 页面 → 第一条 state 命令\n");
        printf("  --smoke-engine    再加一次完整回合\n\n");
        printf("数据根：<exe 目录>\\.agent（角色、会话、语音成品、日志）\n");
    }

    static std::atomic<bool> g_signal_stop{false};

    int gui_main(int argc, char const *argv[])
    {
        std::string ui_arg;
        int smoke = 0;
        int serve_port = 0;
        bool headless = false;
        bool debug_mode = false;
        for (int i = 1; i < argc; i++)
        {
            std::string a = argv[i];
            auto port_arg = [&](const char *flag) -> int
            {
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    const char *s = argv[++i];
                    char *end = nullptr;
                    long v = std::strtol(s, &end, 10);
                    if (!end || *end != '\0' || v < 1 || v > 65535)
                    {
                        fprintf(stderr, "invalid port for %s: '%s' (expected 1..65535)\n", flag, s);
                        return -1;
                    }
                    return (int)v;
                }
                return 8899;
            };
            if (a == "--ui" && i + 1 < argc)
                ui_arg = argv[++i];
            else if (a == "--serve")
            {
                serve_port = port_arg("--serve");
                if (serve_port < 0)
                    return 2;
            }
            else if (a == "--headless")
            {
                headless = true;
                serve_port = port_arg("--headless");
                if (serve_port < 0)
                    return 2;
            }
            else if (a == "--smoke")
                smoke = 1;
            else if (a == "--smoke-engine")
                smoke = 2;
            else if (a == "--debug")
                debug_mode = true;
            else if (a == "--help" || a == "-h")
            {
                print_usage();
                return 0;
            }
            else
            {
                fprintf(stderr, "unknown argument: %s\n", a.c_str());
                print_usage();
                return 1;
            }
        }
#ifdef _WIN32
        // 单实例：重复启动 = 把已有窗口拉到前台（托盘里藏着也要显示出来）
        if (!smoke && !headless)
        {
            HANDLE inst = CreateMutexW(nullptr, FALSE, L"Local\\agent-cpp-desktop");
            DWORD inst_err = GetLastError(); // 必须紧跟 CreateMutexW：静态初始化会清 last-error
            if (inst && inst_err == ERROR_ALREADY_EXISTS)
            {
                if (HWND h = FindWindowW(L"webview", nullptr))
                {
                    if (!IsWindowVisible(h) || IsIconic(h))
                        ShowWindow(h, SW_RESTORE);
                    SetForegroundWindow(h);
                    SwitchToThisWindow(h, TRUE);
                }
                printf("agent already running - switched to the existing instance\n");
                return 0;
            }
        }
#endif
        // 数据根必须在 logger / config / vault 第一次使用之前改好
        cell::root = cell::plat::executable_dir() / ".agent";
        if (smoke)
            cell::root = std::filesystem::temp_directory_path() / "agent-smoke";

        auto &log = cell::sys::logger::instance();
        cell::sys::install_handlers();

        curl_global_init(CURL_GLOBAL_DEFAULT);
        int sodium_rc = sodium_init();
        (void)sodium_rc;

        app_core core;

        // 默认继承 cell.cpp 的全部基础工具（ls/read/write/edit/rg/exec/find/tw/notice/msg）：
        // 沙箱 full-access、autoallow（由 LLM 做执行前的安全判断）、长对话自动压缩。
        // 这里只是进程级初值；core.start() 读到 agent.json 之后会按用户偏好覆盖
        // autoallow / compact_auto（默认都是开）。
        if (auto res = cell::config::load())
            core.cfg = *res;
        else
            log.warn("app", std::format("config load: {}", res.error()));
        core.cfg.tools = true;
        core.cfg.sandbox_mode = "full-access";
        core.cfg.compact_auto = true;
        core.cfg.autoallow = true;
        cell::box::sandbox_mode() = cell::box::SandboxMode::FullAccess;
        cell::box::autoallow_enabled() = true;
        cell::sys::pool_max_setting() = 4;

        // 默认空角色：只有开发模式（--debug / --smoke*）才自动生成测试角色
        core.debug_seed = debug_mode || smoke != 0;
        if (core.debug_seed)
            log.info("app", "debug mode: an empty data root gets a test persona");

        core.start();
        if (core.active.empty())
            log.warn("app", "no persona opened - the UI will show an empty state");

        http_service http(core);
        http.ui_path = ui_arg;
        auto start_http = [&]() -> bool
        {
            if (serve_port <= 0)
                return true;
            std::string err = http.start(serve_port, 4);
            if (!err.empty())
            {
                log.error("http", err);
                return false;
            }
            log.info("http", std::format("serving on http://127.0.0.1:{}/ (desktop={})", serve_port, !headless));
            return true;
        };

        int rc = 0;
        {
            auto on_exit = cell::sys::make_scoped_exit([&]
                                                       {
                try
                {
                    core.begin_shutdown();
                }
                catch (...)
                {
                }
                http.stop();
                try
                {
                    core.shutdown();
                }
                catch (...)
                {
                }
                cell::async_io::flush();
                cell::sys::pool().shutdown();
                cell::net::curl_global_shutdown(); });

            if (headless)
            {
                if (!start_http())
                    return 1;
                std::signal(SIGINT, [](int)
                            { g_signal_stop.store(true); });
                std::signal(SIGTERM, [](int)
                            { g_signal_stop.store(true); });
                log.info("app", "headless mode: Ctrl+C to stop");
                while (!g_signal_stop.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                core.begin_shutdown();
                http.stop();
                core.shutdown();
            }
            else
            {
                // ---- 桌面窗口 -------------------------------------------
                webview::webview w(false, nullptr);
                w.set_title("agent");
                w.set_size(920, 640, WEBVIEW_HINT_MIN);
                w.set_size(1180, 820, WEBVIEW_HINT_NONE);

                // WebView2 在构造函数里已经完成一次性初始化（含 winsock 的
                // WSAStartup/WSACleanup），此时再起 acceptor 才不会被它顺手清掉。
                bool served = false;
                if (serve_port > 0)
                    served = start_http();
                else
                {
                    for (int p = 8899; p <= 8918 && !served; p++)
                    {
                        serve_port = p;
                        served = start_http();
                    }
                    if (!served)
                        log.error("http", "no free port in 8899..8918 - the page boots without a backend");
                }

                // 冒烟测试：页面启动后会发第一条 state 命令
                auto smoke_fired = std::make_shared<std::atomic<bool>>(false);
                if (smoke)
                    core.on_command = [&w, &core, smoke, smoke_fired](const std::string &name)
                    {
                        if (name != "state" || smoke_fired->exchange(true))
                            return;
                        if (smoke == 1)
                        {
                            printf("SMOKE_UI_OK\n");
                            fflush(stdout);
                            w.dispatch([&w]()
                                       { w.terminate(); });
                        }
                        else
                        {
                            printf("SMOKE_UI_OK (probing engine)\n");
                            fflush(stdout);
                            core.bus.add([&w](const std::string &line)
                                         {
                                if (line.find("\"turn_end\"") != std::string::npos)
                                {
                                    printf("SMOKE_ENGINE_OK\n");
                                    fflush(stdout);
                                    w.dispatch([&w]() { w.terminate(); });
                                } });
                            core.exec_command("send", {{"text", "hello from the smoke test"}});
                        }
                    };

#ifdef _WIN32
                // 告诉页面“我跑在桌面壳里”（浏览器打开时没有这个绑定）
                w.bind("host_info", [](std::string) -> std::string
                       { return nlohmann::json{{"desktop", true}, {"os", "windows"}}.dump(); });
#endif
                if (served)
                {
                    try
                    {
                        w.navigate(std::format("http://127.0.0.1:{}/", serve_port));
                    }
                    catch (const std::exception &e)
                    {
                        log.warn("app", std::format("navigate failed ({}), using the embedded page", e.what()));
                        served = false;
                    }
                }
                if (!served)
                {
                    w.set_html(app::kUiHtml);
                    if (smoke)
                    {
                        printf("SMOKE_FAIL (no HTTP page to probe)\n");
                        fflush(stdout);
                        w.dispatch([&]()
                                   { w.terminate(); });
                    }
                }

                log.info("app", std::format("window up: persona={} model={} providers={} serve={}",
                                            core.active, core.cfg.model_label(), core.cfg.providers.size(), serve_port));
#ifdef _WIN32
                if (!smoke)
                {
                    HWND hwnd = nullptr;
                    if (auto win = w.window(); win.ok())
                        hwnd = reinterpret_cast<HWND>(win.value());
                    if (tray::attach(hwnd))
                        log.info("app", "tray: closing the window parks the app in the notification area");
                    else
                        log.warn("app", "tray: icon install failed - closing the window will quit");
                }
#endif
                w.run();
#ifdef _WIN32
                tray::detach();
#endif
                core.begin_shutdown();
                http.stop();
                core.shutdown();
            }
        }
        return rc;
    }
} // namespace app

int main(int argc, char const *argv[])
{
    return app::gui_main(argc, argv);
}
