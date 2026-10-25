// ============================================================================
//  main.cpp —— 启动引导
//
//  桌面模式（Windows 默认）下，进程就是一个 WebView2 应用：
//    - 没有控制台窗口，日志只落 .agent/logs/agent.log；
//    - 内置 HTTP 服务只监听 127.0.0.1，端口被占用时自动换一个临时端口，
//      它是 UI 与频道 webhook 的内部通道，不对外暴露；
//    - 单实例：重复启动会把已有窗口拉到前台。
//
//  启动顺序（每一步失败都给出可操作的错误信息）：
//
//    1. 解析命令行（--root / --settings / --port / --password / --apikey）
//    2. 定位 .agent 数据根，迁移老布局，读配置，播种目录
//    3. 初始化日志、libsodium、口令校验与加密密钥
//    4. 加载提示词 / 工具清单 / 权限规则 / 会话
//    5. 拉起所有 active 频道（Telegram / WeChat / QQ …）
//    6. 启动内部 HTTP 服务（REST + SSE + WebUI）
//    7. 打开 WebView2 窗口
//
//  运行期数据全部落在 <exeDir>/.agent 下，见 core/paths.hpp。
// ============================================================================
#include "core/common.hpp"
#include "core/paths.hpp"
#include "core/config.hpp"
#include "core/log.hpp"
#include "core/crypto.hpp"
#include "core/sysinfo.hpp"
#include "core/tiny_sha.h"
#include "agent/tools.hpp"
#include "agent/runtime.hpp"
#include "agent/providers.hpp"
#include "agent/permissions.hpp"
#include "channels/channel.hpp"
#include "server/http_server.hpp"
#include "server/routes.hpp"
#include "desktop/webview2.hpp"

#include <csignal>

namespace
{
    std::atomic<bool> g_shutdown{false};
    bool g_gui_mode = false;

    void on_signal(int)
    {
        g_shutdown.store(true);
    }

    void print_usage()
    {
        std::cout <<
            R"(agent.cpp — C++ AI companion runtime

Usage: agentcpp [options]

  --root <dir>        数据根目录（默认 <exeDir>/.agent）
  --settings <file>   配置文件路径（默认 <root>/settings.json）
  -p, --port <port>   内部 HTTP 端口（默认取配置 server.port，占用时回退临时端口）
  --host <addr>       监听地址（仅 --no-gui 有效；桌面模式固定 127.0.0.1）
  --password <pw>     WebUI / 数据加密口令（首次运行会生成 sys/key）
  --apikey <key>      临时覆盖当前供应商的 API Key
  --gui               打开 WebView2 桌面窗口（Windows 默认开启）
  --no-gui            只跑服务，不打开窗口（服务器 / 远程部署用）
  --devtools          允许打开 WebView2 开发者工具（排查界面问题用）
  --no-tray           不常驻系统托盘，关闭窗口即退出
  --width <px>        窗口宽度（默认 1280）
  --height <px>       窗口高度（默认 860）
  -h, --help          显示本帮助
  -v, --version       显示版本

第一次运行会在 <exeDir>/.agent 下生成：
  settings.json   主配置
  webui/          WebUI 静态资源（单文件 webui.html 也支持）
  workspace/      Agent 工作区（提示词 / 会话 / 记忆 / 工具）
  logs/agent.log  运行日志（桌面模式不显示控制台，排障看这里）
)";
    }

    // 口令校验 + 派生加密密钥 + 注入 WebUI 密码哈希
    void init_auth(const std::string &password)
    {
        const fs::path key_path = config::sys_dir() / "key";
        std::error_code ec;

        if (!fs::exists(key_path, ec))
        {
            if (password.empty())
            {
                logx::warn("auth", "首次运行未设置口令，WebUI 将不校验密码");
            }
            else
            {
                char stored[crypto_pwhash_STRBYTES];
                if (crypto_pwhash_str(stored, password.c_str(), password.size(),
                                      crypto_pwhash_OPSLIMIT_MODERATE,
                                      crypto_pwhash_MEMLIMIT_MODERATE) != 0)
                    throw std::runtime_error("failed to hash password");
                tool_unit::writeFile(paths::to_utf8(key_path), std::string(stored));
                logx::info("auth", "password hash written to " + paths::to_utf8(key_path));
            }
        }
        else
        {
            if (password.empty())
                throw std::runtime_error("password required: " + paths::to_utf8(key_path) +
                                         " exists, pass --password <pw>");
            std::string stored = tool_unit::readFile(paths::to_utf8(key_path));
            if (!stored.empty() && stored.back() == '\n')
                stored.pop_back();
            if (crypto_pwhash_str_verify(stored.c_str(), password.c_str(), password.size()) != 0)
                throw std::runtime_error("incorrect password");
            logx::info("auth", "password verified");
        }

        crypto_context::init_key_from_password(password);

        std::uint8_t digest[SHA3_256_DIGEST_SIZE];
        if (!SHA3_256(reinterpret_cast<const std::uint8_t *>(password.data()), password.size(), digest))
            throw std::runtime_error("SHA3-256 failed");
        run_unit::settings["webui_password"] = agent::hex(digest, SHA3_256_DIGEST_SIZE);
    }

    // 读 cs.txt / tools.json / data.json / 会话索引
    void init_runtime_state()
    {
        const fs::path ws = config::workspace_dir();

        run_unit::cs_prompt = tool_unit::readFile(paths::to_utf8(config::sys_dir() / "cs.txt"));

        const fs::path tools_json = config::tools_dir() / "tools.json";
        std::error_code ec;
        if (fs::exists(tools_json, ec))
            run_unit::tools_list = json::parse(tool_unit::readFile(paths::to_utf8(tools_json)));
        else
            run_unit::tools_list = json::array();

        run_unit::agent_data_manager = run_unit::DataManager(paths::to_utf8(ws));
        run_unit::agent_session_manager.init(paths::to_utf8(ws));

        agent::load_prompts();
        perm_unit::manager().init(paths::to_utf8(ws));
    }
} // namespace

int main(int argc, char *argv[])
{
    try
    {
        std::string root_override;
        std::string settings_path;
        std::string password;
        std::string apikey;
        int port_override = -1;
        std::string host_override;
#ifdef _WIN32
        bool gui = true; // Windows 默认带窗口，双击即用
#else
        bool gui = false;
#endif
        int win_w = 1280, win_h = 860;
        bool devtools = false;
        bool tray = true;

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto need = [&](const char *name) -> std::string
            {
                if (i + 1 >= argc)
                    throw std::runtime_error(std::string("missing value after ") + name);
                return argv[++i];
            };
            if (arg == "--root")
                root_override = need("--root");
            else if (arg == "--settings")
                settings_path = need("--settings");
            else if (arg == "-p" || arg == "--port")
                port_override = std::stoi(need("--port"));
            else if (arg == "--host")
                host_override = need("--host");
            else if (arg == "--password")
                password = need("--password");
            else if (arg == "--apikey")
                apikey = need("--apikey");
            else if (arg == "--gui")
                gui = true;
            else if (arg == "--no-gui")
                gui = false;
            else if (arg == "--devtools")
                devtools = true;
            else if (arg == "--no-tray")
                tray = false;
            else if (arg == "--width")
                win_w = std::stoi(need("--width"));
            else if (arg == "--height")
                win_h = std::stoi(need("--height"));
            else if (arg == "-h" || arg == "--help")
            {
                print_usage();
                return 0;
            }
            else if (arg == "-v" || arg == "--version")
            {
                std::cout << "agent.cpp 2.0.0" << std::endl;
                return 0;
            }
            else
            {
                std::cerr << "unknown option: " << arg << "\n\n";
                print_usage();
                return 2;
            }
        }

        // ---- 0. 桌面模式：单实例 + 隐藏控制台 ------------------------------
        g_gui_mode = gui;
        if (gui)
        {
            desktop::hide_console_if_standalone();
            if (!desktop::single_instance_acquire())
            {
                if (!desktop::activate_existing())
                    std::cerr << "agent.cpp is already running" << std::endl;
                return 0;
            }
        }

        // ---- 1. 数据根 ----------------------------------------------------
        if (!root_override.empty())
            paths::set_agent_root(root_override);

        if (!gui)
            std::cout << get_system_status() << std::endl;

        // ---- 2. 配置 ------------------------------------------------------
        config::migrate_legacy();
        const bool created = config::load(settings_path);
        config::ensure_layout();
        config::validate();
        if (created)
            logx::info("config", "created default settings at " + paths::to_utf8(config::file()));

        // ---- 3. 日志 / 加密 ----------------------------------------------
        logx::set_console(!gui);
        logx::init(config::logs_dir() / "agent.log");
        logx::info("boot", "data root: " + paths::to_utf8(paths::agent_root()));
        logx::info("boot", "workspace: " + paths::to_utf8(config::workspace_dir()));
        logx::info("boot", "webui:     " + paths::to_utf8(config::webui_path()));

        if (sodium_init() < 0)
            throw std::runtime_error("libsodium initialization failed");

        init_auth(password);

        // ---- 4. 运行时状态 ------------------------------------------------
        agent::init_from_settings();
        if (!apikey.empty())
        {
            // 临时覆盖当前供应商的 key（不落盘）
            const std::string id = config::str("current_provider", "openai-default");
            json *p = agent::find_provider(id);
            if (p)
                agent::switch_provider(p->value("type", "openai"),
                                       p->value("server_address", ""), apikey);
        }
        init_runtime_state();

        // ---- 5. 频道 ------------------------------------------------------
        channels::start_all();

        // ---- 6. 内部 HTTP 服务（桌面模式只监听回环） ----------------------
        std::string host = host_override.empty() ? config::obj("server").value("host", "127.0.0.1")
                                                 : host_override;
        int port = port_override > 0 ? port_override
                                     : static_cast<int>(config::obj("server").value("port", 8080));

        if (gui && host != "127.0.0.1" && host != "localhost" && host != "::1")
        {
            logx::info("server", "desktop mode binds loopback only (server.host=" + host + " ignored)");
            host = "127.0.0.1";
        }

        http::Router router;
        server::register_routes(router);

        std::unique_ptr<http::Server> http_server(
            new http::Server(host, static_cast<unsigned short>(port)));
        http_server->attach(&router);
        http_server->allow_ephemeral_fallback(gui);
        if (!http_server->start())
            throw std::runtime_error("failed to bind " + host + ":" + std::to_string(port));
        port = http_server->port();

        const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
        if (gui)
        {
            logx::info("server", "internal endpoint " + url);
        }
        else
        {
            std::cout << "WebUI: " << url << std::endl;
            std::cout << "Data:  " << paths::to_utf8(paths::agent_root()) << std::endl;
        }

        std::signal(SIGINT, on_signal);
        std::signal(SIGTERM, on_signal);

        // ---- 7. 桌面外壳 ---------------------------------------------------
        if (gui)
        {
            if (desktop::available())
            {
                desktop::WindowOptions wo;
                wo.title = config::agent_name() + " · agent.cpp";
                wo.url = url;
                wo.width = win_w;
                wo.height = win_h;
                wo.devtools = devtools;
                wo.tray = tray;
                wo.close_to_tray = config::obj("window").value("close_to_tray", true);
                wo.tray_tooltip = config::agent_name() + " · agent.cpp";
                logx::info("desktop", "opening WebView2 window: " + url);
                int rc = desktop::run(wo);
                logx::info("desktop", "window closed (rc=" + std::to_string(rc) + ")");
                g_shutdown.store(true);
            }
            else
            {
                logx::warn("desktop", "WebView2 runtime unavailable — falling back to the default browser");
                desktop::open_in_browser(url);
            }
        }

        while (!g_shutdown.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        logx::info("boot", "shutting down");
        http_server->stop();
        config::save();
        desktop::single_instance_release();
        return 0;
    }
    catch (const std::exception &e)
    {
        if (g_gui_mode)
        {
            logx::error("fatal", e.what());
            desktop::fatal_dialog("agent.cpp 启动失败", e.what());
        }
        std::cerr << "Fatal error: " << e.what() << '\n';
        desktop::single_instance_release();
        return 1;
    }
}
