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

#include <iostream>
#include <thread>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#endif

namespace agent {

std::atomic<bool> g_shutdown{false};

void signal_handler(int signum) {
    g_shutdown.store(true);
    g_is_shutting_down.store(true);
}

#ifdef _WIN32
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    switch (ctrl_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            g_shutdown.store(true);
            g_is_shutting_down.store(true);
            return TRUE;
        default:
            return FALSE;
    }
}
#endif

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
}

void start_background_router() {
#ifdef AGENT_ENABLE_ROUTER
    auto& cfg = Config::instance();
    if (!cfg.router_enabled) return;

    auto& router = ModuleRegistry::instance().require<Router>();

    RouterConfig router_cfg;
    router_cfg.port = cfg.router_port;
    router_cfg.bind_address = cfg.router_bind;
    router_cfg.enable_tls = cfg.router_tls;
    router.configure(router_cfg);

    std::thread([&router]() {
        try {
            router.start();
            LOG_INFO("Main", "Background HTTP server started");
        } catch (const std::exception& e) {
            LOG_ERROR("Main", std::string("Router start failed: ") + e.what());
        }
    }).detach();
#endif
}

void graceful_shutdown() {
    LOG_INFO("Main", "Graceful shutdown initiated");

    try {
        ModuleRegistry::instance().shutdown_all();
    } catch (const BaseException& e) {
        LOG_ERROR("Main", std::string("Shutdown error: ") + e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("Main", std::string("Shutdown error: ") + e.what());
    } catch (...) {
        LOG_ERROR("Main", "Unknown error during shutdown");
    }

    LOG_INFO("Main", "Shutdown complete");
}

} // namespace agent

int main(int argc, char* argv[]) {
    using namespace agent;

    Config::instance();

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
#ifdef SIGBREAK
    std::signal(SIGBREAK, signal_handler);
#endif
#ifdef _WIN32
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#endif

    register_all_modules();

    System::CLIOptions opts;
    {
        auto* system_component = ModuleRegistry::instance().get<System>();
        opts = system_component->parse_args(argc, argv);
    }

    if (opts.help) {
        std::cout << "agent.cpp v" << AGENT_VERSION << std::endl;
        std::cout << "Usage: agent [options]" << std::endl;
        std::cout << "  --project, -p DIR  Set project directory" << std::endl;
        std::cout << "  --session, -s ID   Use specific session" << std::endl;
        std::cout << "  --command, -c CMD  Execute command and exit" << std::endl;
        std::cout << "  --reset-config     Reset all configuration" << std::endl;
        std::cout << "  --version, -v      Show version" << std::endl;
        std::cout << "  --help, -h         Show this help" << std::endl;
        std::cout << "\nDefault: Start HTTP API server with WebUI" << std::endl;
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
        graceful_shutdown();
        return 0;
    }

    // Default: start HTTP server with WebUI
#ifdef AGENT_ENABLE_ROUTER
    auto& router = ModuleRegistry::instance().require<Router>();
    auto& cfg = Config::instance();

    RouterConfig router_cfg;
    router_cfg.port = cfg.router_port;
    router_cfg.bind_address = cfg.router_bind;
    router_cfg.enable_tls = cfg.router_tls;
    router_cfg.cert_path = cfg.router_cert_path;
    router_cfg.key_path = cfg.router_key_path;
    router.configure(router_cfg);

    std::string proto = cfg.router_tls ? "https" : "http";
    std::cout << "agent.cpp v" << AGENT_VERSION << std::endl;
    std::cout << "Starting " << (cfg.router_tls ? "HTTPS" : "HTTP") << " server on " << proto << "://" << router_cfg.bind_address << ":" << router_cfg.port << std::endl;
    std::cout << "WebUI available at " << proto << "://" << router_cfg.bind_address << ":" << router_cfg.port << "/" << std::endl;
    router.start();

    while (!g_shutdown.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    router.stop();
    graceful_shutdown();
    return 0;
#else
    LOG_ERROR("Main", "Router mode not available (compiled without AGENT_ENABLE_ROUTER)");
    std::cerr << "Router not available" << std::endl;
    return 1;
#endif
}
