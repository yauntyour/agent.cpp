#include "core/module.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include "core/exception.hpp"
#include "components/provider/provider.hpp"
#include "components/tools/tools.hpp"
#include "components/session/session.hpp"
#include "components/memory/memory.hpp"
#include "components/permission/permission.hpp"
#include "components/notice/notice.hpp"
#include "components/edit_history/edit_history.hpp"
#include "components/service/service.hpp"
#include "components/mcp/mcp.hpp"
#include "components/system/system.hpp"
#include "components/agent/agent.hpp"

#ifdef AGENT_ENABLE_LSP
#include "components/lsp/lsp.hpp"
#endif

#ifdef AGENT_ENABLE_ROUTER
#include "components/router/router.hpp"
#endif

#ifdef AGENT_ENABLE_TUI
#include "components/tui/tui.hpp"
#endif

#include <iostream>
#include <thread>
#include <csignal>

namespace agent {

std::atomic<bool> g_shutdown{false};

void signal_handler(int signum) {
    g_shutdown.store(true);
    g_is_shutting_down.store(true);
}

void print_banner() {
    std::cout << R"(
   __ _  ___ _ __   ___ _ __   ___ _ __    ___ _ __  _ __
  / _` |/ _ \ '_ \ / _ \ '_ \ / _ \ '_ \  / __| '_ \| '_ \
 | (_| |  __/ | | |  __/ | | |  __/ |_) | \__ \ |_) | |_) |
  \__,_|\___|_| |_|\___|_| |_|\___| .__/  |___/ .__/| .__/
                                  |_|         |_|   |_|
)" << std::endl;
    std::cout << "  agent.cpp v" << AGENT_VERSION << " — Modular AI Coding Agent" << std::endl;
    std::cout << "  Type /help for commands, Ctrl+C to exit\n" << std::endl;
}

void register_all_modules() {
    auto& registry = ModuleRegistry::instance();

    registry.register_module(std::make_unique<Provider>());
    registry.register_module(std::make_unique<Tools>());
    registry.register_module(std::make_unique<SessionManager>());
    registry.register_module(std::make_unique<Memory>());
    registry.register_module(std::make_unique<Permission>());
    registry.register_module(std::make_unique<Notice>());
    registry.register_module(std::make_unique<EditHistory>());
    registry.register_module(std::make_unique<Service>());
    registry.register_module(std::make_unique<MCP>());
    registry.register_module(std::make_unique<System>());
    registry.register_module(std::make_unique<Agent>());

#ifdef AGENT_ENABLE_LSP
    registry.register_module(std::make_unique<LSP>());
#endif

#ifdef AGENT_ENABLE_ROUTER
    registry.register_module(std::make_unique<Router>());
#endif

#ifdef AGENT_ENABLE_TUI
    registry.register_module(std::make_unique<TUI>());
#endif
}

int run_cli_mode(const System::CLIOptions& opts) {
    auto& registry = ModuleRegistry::instance();
    auto& system = registry.require<System>();
    auto& agent = registry.require<Agent>();
    auto& sessions = registry.require<SessionManager>();

    if (!opts.session_id.empty()) {
        sessions.set_current(opts.session_id);
    }

    auto current = sessions.current_session();
    if (current.id.empty()) {
        current = sessions.new_session(opts.project_dir.empty() ? "." : opts.project_dir);
    }

    std::cout << "Session: " << current.name << " (" << current.id << ")" << std::endl;
    std::cout << "Model: " << registry.require<Provider>().current_model().name << std::endl;

    if (!opts.command.empty()) {
        auto result = agent.execute(opts.command,
            [](std::string_view type, std::string_view content) {
                if (type == "thinking") std::cout << content << std::flush;
            }, true);
        std::cout << std::endl;
        if (!result.success) {
            LOG_ERROR("Main", "Command execution failed: " + result.error);
            std::cerr << "Error: " << result.error << std::endl;
            return 1;
        }
        return 0;
    }

    std::string line;
    while (!g_shutdown.load()) {
        std::cout << "\n> ";
        std::getline(std::cin, line);

        if (line.empty()) continue;

        if (line[0] == '/') {
            system.execute_command(line);
        } else {
            auto result = agent.execute(line,
                [](std::string_view type, std::string_view content) {
                    if (type == "thinking") std::cout << content << std::flush;
                }, true);
            std::cout << std::endl;
            if (!result.success) {
                LOG_ERROR("Main", "Execution failed: " + result.error);
                std::cerr << "Error: " << result.error << std::endl;
            }
        }
    }

    return 0;
}

int run_tui_mode(const System::CLIOptions& opts) {
#ifdef AGENT_ENABLE_TUI
    auto& registry = ModuleRegistry::instance();
    auto& tui = registry.require<TUI>();
    auto& sys = registry.require<System>();
    auto& agent = registry.require<Agent>();
    auto& sessions = registry.require<SessionManager>();
    auto& config = Config::instance();

    if (!opts.session_id.empty()) {
        sessions.set_current(opts.session_id);
    }

    auto current = sessions.current_session();
    if (current.id.empty()) {
        current = sessions.new_session(opts.project_dir.empty() ? "." : opts.project_dir);
    }

    auto& provider = registry.require<Provider>();
    auto model = provider.current_model();

    tui.set_model_info("default", model.name, "auto");
    tui.set_lsp_status("idle");
    tui.set_mcp_status("idle");

    Theme theme;
    if (config.tui_theme != "default" && config.themes.count(config.tui_theme)) {
        auto& tc = config.themes[config.tui_theme];
        theme = Theme::from_config(
            config.tui_theme,
            tc.primary_color, tc.secondary_color, tc.success_color,
            tc.warning_color, tc.error_color,
            tc.background_color, tc.foreground_color,
            tc.status_bar_color, tc.input_color, tc.border_color
        );
    }
    tui.set_theme(theme);

    {
        auto cmds = sys.list_commands();
        std::vector<std::string> completions;
        for (auto& c : cmds) {
            completions.push_back(c.name);
            for (auto& a : c.aliases) completions.push_back(a);
        }
        tui.set_command_completions(completions);
    }

    tui.on_input_submit([&](std::string_view text) {
        try {
            agent.execute(text,
                [&](std::string_view type, std::string_view content) {
                    if (type == "thinking") tui.append_content(content);
                }, true);
        } catch (const BaseException& e) {
            LOG_ERROR("Main", std::string("TUI execution error: ") + e.what());
            tui.append_content("\n[ERROR] " + std::string(e.error_code_name()) + ": " + e.message());
        } catch (const std::exception& e) {
            LOG_ERROR("Main", std::string("TUI execution error: ") + e.what());
            tui.append_content("\n[ERROR] " + std::string(e.what()));
        }
    });

    tui.on_command([&](std::string_view cmd) {
        sys.execute_command(cmd);
    });

    tui.run();
    return 0;
#else
    LOG_ERROR("Main", "TUI mode not available (compiled without AGENT_ENABLE_TUI)");
    std::cerr << "TUI not available" << std::endl;
    return 1;
#endif
}

int run_router_mode(const System::CLIOptions& opts) {
#ifdef AGENT_ENABLE_ROUTER
    auto& router = ModuleRegistry::instance().require<Router>();
    auto& cfg = Config::instance();

    RouterConfig router_cfg;
    router_cfg.port = cfg.router_port;
    router_cfg.bind_address = cfg.router_bind;
    router_cfg.enable_tls = cfg.router_tls;
    router.configure(router_cfg);

    std::cout << "Starting HTTP server on " << router_cfg.bind_address << ":" << router_cfg.port << std::endl;
    router.start();

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    router.stop();
    return 0;
#else
    LOG_ERROR("Main", "Router mode not available (compiled without AGENT_ENABLE_ROUTER)");
    std::cerr << "Router not available" << std::endl;
    return 1;
#endif
}

} // namespace agent

int main(int argc, char* argv[]) {
    using namespace agent;

    Config::instance();

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    register_all_modules();

    System::CLIOptions opts;
    {
        auto* system_component = ModuleRegistry::instance().get<System>();
        opts = system_component->parse_args(argc, argv);
    }

    if (opts.help) {
        std::cout << "agent.cpp v" << AGENT_VERSION << std::endl;
        std::cout << "Usage: agent [options]" << std::endl;
        std::cout << "  --tui, -t          Launch TUI interface" << std::endl;
        std::cout << "  --router, -r       Launch HTTP API server" << std::endl;
        std::cout << "  --daemon, -d       Run as daemon" << std::endl;
        std::cout << "  --project, -p DIR  Set project directory" << std::endl;
        std::cout << "  --session, -s ID   Use specific session" << std::endl;
        std::cout << "  --command, -c CMD  Execute command and exit" << std::endl;
        std::cout << "  --reset-config     Reset all configuration" << std::endl;
        std::cout << "  --version, -v      Show version" << std::endl;
        std::cout << "  --help, -h         Show this help" << std::endl;
        return 0;
    }

    auto& system_component = ModuleRegistry::instance().require<System>();

    try {
        if (opts.reset_config) {
            system_component.init(System::InitMode::Reset);
        } else {
            system_component.init(System::InitMode::Normal);
        }
    } catch (const BaseException& e) {
        LOG_ERROR("Main", std::string("Initialization failed: ") + e.what());
        std::cerr << "Init error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        LOG_ERROR("Main", std::string("Initialization failed: ") + e.what());
        std::cerr << "Init error: " << e.what() << std::endl;
    }

    if (!opts.project_dir.empty()) {
        system_component.set_project_dir(opts.project_dir);
        Config::instance().set_project_dir(opts.project_dir);
    }

    if (opts.version) {
        std::cout << "agent.cpp v" << AGENT_VERSION << std::endl;
        try {
            ModuleRegistry::instance().shutdown_all();
        } catch (const BaseException& e) {
            LOG_ERROR("Main", std::string("Shutdown error: ") + e.what());
            std::cerr << "Shutdown error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            LOG_ERROR("Main", std::string("Shutdown error: ") + e.what());
            std::cerr << "Shutdown error: " << e.what() << std::endl;
        }
        return 0;
    }

    if (opts.tui_mode) {
        return run_tui_mode(opts);
    } else if (opts.router_mode) {
        return run_router_mode(opts);
    } else {
        print_banner();
        return run_cli_mode(opts);
    }
}
