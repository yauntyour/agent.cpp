#include "components/system/system.hpp"
#include "components/edit_history/edit_history.hpp"
#include "components/mcp/mcp.hpp"
#ifdef AGENT_ENABLE_LSP
#include "components/lsp/lsp.hpp"
#endif
#include "utils/fs.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace agent {

using json = nlohmann::json;

void System::on_initialize() {
    load_keychain();
    setup_commands();
}

void System::on_shutdown() {
    stop_channels();
    try { save_keychain(); } catch (...) {}
}

void System::init(InitMode mode) {
    auto& cfg = Config::instance();
    cfg.load();

    if (mode == InitMode::Reset) {
        cfg.reset();
    }

    if (cfg.providers.empty()) {
        Config::ProviderConfig local;
        local.name = "llama-server";
        local.type = "llama_server";
        local.api_base = "http://localhost:11434/v1";
        local.model = "default";
        local.context_length = 8192;
        local.max_tokens = 4096;
        local.temperature = 0.7f;
        local.supports_tools = true;
        cfg.providers.push_back(local);

        Config::ProviderConfig ollama;
        ollama.name = "ollama";
        ollama.type = "ollama";
        ollama.api_base = "http://localhost:11434";
        ollama.model = "llama3";
        ollama.context_length = 8192;
        ollama.max_tokens = 4096;
        cfg.providers.push_back(ollama);

        cfg.default_provider = "llama-server";
    }

    cfg.save();

    cfg.agent_dir();
    cfg.session_dir();
    cfg.memory_dir();
    cfg.edit_history_dir();
    cfg.assets_dir();
}

void System::hot_reload() {
    auto& cfg = Config::instance();
    cfg.load();
}

void System::restart() {
    auto& registry = ModuleRegistry::instance();
    registry.shutdown_all();

    init(InitMode::Normal);
    registry.initialize_all();
}

void System::shutdown_system() {
    auto& registry = ModuleRegistry::instance();
    registry.shutdown_all();
}

System::CLIOptions System::parse_args(int argc, char* argv[]) {
    CLIOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tui" || arg == "-t") {
            opts.tui_mode = true;
        } else if (arg == "--router" || arg == "-r") {
            opts.router_mode = true;
        } else if (arg == "--daemon" || arg == "-d") {
            opts.daemon_mode = true;
        } else if (arg == "--project" || arg == "-p") {
            if (i + 1 < argc) opts.project_dir = argv[++i];
        } else if (arg == "--session" || arg == "-s") {
            if (i + 1 < argc) opts.session_id = argv[++i];
        } else if (arg == "--command" || arg == "-c") {
            if (i + 1 < argc) opts.command = argv[++i];
        } else if (arg == "--reset-config") {
            opts.reset_config = true;
        } else if (arg == "--version" || arg == "-v") {
            opts.version = true;
        } else if (arg == "--help" || arg == "-h") {
            opts.help = true;
        }
    }

    m_opts = opts;
    return opts;
}

void System::register_command(SystemCommand cmd) {
    m_commands[cmd.name] = std::move(cmd);
}

void System::execute_command(std::string_view line) {
    std::string cmd_line(line);
    auto space = cmd_line.find(' ');
    std::string cmd_name = (space == std::string::npos) ? cmd_line : cmd_line.substr(0, space);
    std::vector<std::string> args;

    if (space != std::string::npos) {
        std::string rest = cmd_line.substr(space + 1);
        // Simple arg parsing (space-separated)
        std::stringstream ss(rest);
        std::string arg;
        while (ss >> arg) {
            args.push_back(arg);
        }
    }

    auto it = m_commands.find(cmd_name);
    if (it != m_commands.end()) {
        it->second.handler(args);
    } else {
        // Check aliases
        bool found = false;
        for (auto& [name, cmd] : m_commands) {
            for (auto& alias : cmd.aliases) {
                if (alias == cmd_name) {
                    cmd.handler(args);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            std::cerr << "Unknown command: " << cmd_name << std::endl;
        }
    }
}

std::vector<SystemCommand> System::list_commands() const {
    std::vector<SystemCommand> cmds;
    for (auto& [name, cmd] : m_commands) {
        cmds.push_back(cmd);
    }
    return cmds;
}

void System::setup_commands() {
    register_command({"/new", "Create a new session", "/new [name]", {"/n", "/create"},
        [this](const std::vector<std::string>& args) {
            auto& sessions = ModuleRegistry::instance().require<SessionManager>();
            auto name = args.empty() ? "" : args[0];
            auto info = sessions.new_session(m_opts.project_dir, name);
            std::cout << "Created session: " << info.id << " (" << info.name << ")" << std::endl;
        }});

    register_command({"/model", "Switch model", "/model <provider_id> <model_id>", {"/m"},
        [this](const std::vector<std::string>& args) {
            auto& provider = ModuleRegistry::instance().require<Provider>();
            if (args.size() >= 1) {
                provider.set_current(args[0]);
            }
            if (args.size() >= 2) {
                provider.set_model(args[1]);
            }
            std::cout << "Current model: " << provider.current_model().name << std::endl;
        }});

    register_command({"/sessions", "List sessions", "/sessions [search]", {"/ss"},
        [this](const std::vector<std::string>& args) {
            auto& sessions = ModuleRegistry::instance().require<SessionManager>();
            auto list = args.empty() ? sessions.list_sessions("") : sessions.search_sessions(args[0]);
            for (auto& s : list) {
                std::cout << s.id << " - " << s.name << " (" << s.message_count << " msgs)" << std::endl;
            }
        }});

    register_command({"/session", "Switch to session", "/session <session_id>", {"/s"},
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                std::cerr << "Usage: /session <session_id>" << std::endl;
                return;
            }
            auto& sessions = ModuleRegistry::instance().require<SessionManager>();
            sessions.set_current(args[0]);
            std::cout << "Switched to session: " << args[0] << std::endl;
        }});

    register_command({"/delete", "Delete session", "/delete <session_id>", {"/del", "/d"},
        [this](const std::vector<std::string>& args) {
            if (args.empty()) return;
            auto& sessions = ModuleRegistry::instance().require<SessionManager>();
            sessions.delete_session(args[0]);
            std::cout << "Deleted session: " << args[0] << std::endl;
        }});

    register_command({"/memory", "Search or manage memories", "/memory [search|list|save] ...", {"/mem"},
        [this](const std::vector<std::string>& args) {
            auto& mem = ModuleRegistry::instance().require<Memory>();
            auto all = mem.list_all();
            std::cout << "Memories (" << all.size() << "):" << std::endl;
            for (auto& m : all) {
                std::cout << "  [" << m.category << "] " << m.title << std::endl;
            }
        }});

    register_command({"/history", "Show edit history", "/history", {"/hist"},
        [this](const std::vector<std::string>& args) {
            auto& history = ModuleRegistry::instance().require<EditHistory>();
            auto records = history.list_records(50);
            for (size_t i = 0; i < records.size(); ++i) {
                auto& r = records[i];
                std::cout << i << ": [" << r.tool_name << "] " << r.file_path << std::endl;
            }
        }});

    register_command({"/reset", "Reset system config", "/reset", {},
        [this](const std::vector<std::string>&) {
            init(InitMode::Reset);
            std::cout << "System reset to defaults." << std::endl;
        }});

    register_command({"/mcp", "Manage MCP servers", "/mcp [list|start|stop] [server_id]", {},
        [this](const std::vector<std::string>& args) {
            auto* mcp = ModuleRegistry::instance().get<MCP>();
            if (!mcp) { std::cerr << "MCP component not available" << std::endl; return; }

            if (args.empty() || args[0] == "list") {
                auto servers = mcp->list_servers();
                if (servers.empty()) { std::cout << "No MCP servers configured." << std::endl; return; }
                for (auto& s : servers) {
                    std::string status = mcp->is_server_running(s.id) ? "running" : "stopped";
                    std::cout << "  " << s.id << " (" << s.name << ") [" << status << "] - " << s.command << std::endl;
                    auto tools = mcp->list_tools(s.id);
                    for (auto& t : tools) {
                        std::cout << "    tool: " << t.tool_name << " - " << t.description << std::endl;
                    }
                }
            } else if (args[0] == "start" && args.size() > 1) {
                mcp->start_server(args[1]);
                std::cout << "Started MCP server: " << args[1] << std::endl;
            } else if (args[0] == "stop" && args.size() > 1) {
                mcp->stop_server(args[1]);
                std::cout << "Stopped MCP server: " << args[1] << std::endl;
            }
        }});

    register_command({"/lsp", "Manage LSP servers", "/lsp [list|start|stop] [server_id]", {},
        [this](const std::vector<std::string>& args) {
#ifdef AGENT_ENABLE_LSP
            auto* lsp = ModuleRegistry::instance().get<LSP>();
            if (!lsp) { std::cerr << "LSP component not available" << std::endl; return; }

            if (args.empty() || args[0] == "list") {
                auto servers = lsp->list_servers();
                if (servers.empty()) { std::cout << "No LSP servers configured." << std::endl; return; }
                for (auto& s : servers) {
                    std::string status = lsp->is_server_running(s.id) ? "running" : "stopped";
                    std::cout << "  " << s.id << " [" << s.language << "] [" << status << "] - " << s.command << std::endl;
                }
            } else if (args[0] == "start" && args.size() > 1) {
                lsp->start_server(args[1]);
                std::cout << "Started LSP server: " << args[1] << std::endl;
            } else if (args[0] == "stop" && args.size() > 1) {
                lsp->stop_server(args[1]);
                std::cout << "Stopped LSP server: " << args[1] << std::endl;
            }
#else
            std::cerr << "LSP not enabled (build with AGENT_ENABLE_LSP=ON)" << std::endl;
#endif
        }});

    register_command({"/provider", "Manage model providers", "/provider [list|add|switch] ...", {"/p"},
        [this](const std::vector<std::string>& args) {
            auto& cfg = Config::instance();
            auto& provider = ModuleRegistry::instance().require<Provider>();

            if (args.empty() || args[0] == "list") {
                std::cout << "Providers:" << std::endl;
                for (auto& p : cfg.providers) {
                    std::string current = (p.name == cfg.default_provider) ? " *" : "";
                    std::cout << "  " << p.name << current << " [" << p.type << "] "
                              << p.api_base << " model:" << p.model << std::endl;
                }
            } else if (args[0] == "switch" && args.size() > 1) {
                cfg.default_provider = args[1];
                provider.set_current(args[1]);
                if (args.size() > 2) {
                    cfg.default_model = args[2];
                    provider.set_model(args[2]);
                }
                cfg.save();
                std::cout << "Switched to provider: " << args[1] << std::endl;
            }
        }});

    register_command({"/config", "View or set configuration", "/config [key] [value]", {},
        [this](const std::vector<std::string>& args) {
            auto& cfg = Config::instance();
            if (args.empty()) {
                std::cout << "Config file: " << cfg.agent_dir().string() << "/config.json" << std::endl;
                std::cout << "  default_provider: " << cfg.default_provider << std::endl;
                std::cout << "  default_model: " << cfg.default_model << std::endl;
                std::cout << "  max_context_tokens: " << cfg.max_context_tokens << std::endl;
                std::cout << "  max_mpc_rounds: " << cfg.max_mpc_rounds << std::endl;
                std::cout << "  stream_output: " << (cfg.stream_output ? "true" : "false") << std::endl;
            }
        }});

    register_command({"/quit", "Exit agent", "/quit", {"/q", "/exit", "/bye"},
        [this](const std::vector<std::string>&) {
            std::cout << "Shutting down..." << std::endl;
            g_is_shutting_down.store(true);
            shutdown_system();
            exit(0);
        }});

    register_command({"/help", "Show help", "/help [command]", {"/h", "/?"},
        [this](const std::vector<std::string>& args) {
            if (args.empty()) {
                std::cout << "Available commands:" << std::endl;
                for (auto& [name, cmd] : m_commands) {
                    std::cout << "  " << name << " - " << cmd.description << std::endl;
                }
            } else {
                auto it = m_commands.find(args[0]);
                if (it != m_commands.end()) {
                    std::cout << it->second.name << " - " << it->second.description << std::endl;
                    std::cout << "Usage: " << it->second.usage << std::endl;
                }
            }
        }});
}

// ── Keychain ──────────────────────────────────────────────────────
void System::load_keychain() {
    auto path = Config::instance().keychain_path();
    if (!fs::exists(path)) return;
}

void System::save_keychain() {
    auto path = Config::instance().keychain_path();
}

void System::store_key(std::string_view key_id, std::string_view value, std::string_view password) {
    auto dk = crypto::derive_key(password);
    auto encrypted = crypto::encrypt_string(value, dk.key);

    auto path = Config::instance().keychain_path() / (std::string(key_id) + ".enc");
    if (!fs::exists(path.parent_path())) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(dk.salt.data()), dk.salt.size());
    f << encrypted;
}

std::optional<std::string> System::retrieve_key(std::string_view key_id, std::string_view password) {
    auto path = Config::instance().keychain_path() / (std::string(key_id) + ".enc");
    if (!fs::exists(path)) return std::nullopt;

    std::ifstream f(path, std::ios::binary);
    std::array<unsigned char, crypto_pwhash_SALTBYTES> salt;
    f.read(reinterpret_cast<char*>(salt.data()), salt.size());

    std::string encrypted((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());

    auto dk = crypto::derive_key_with_salt(password, salt);
    return crypto::decrypt_string(encrypted, dk.key);
}

void System::delete_key(std::string_view key_id) {
    auto path = Config::instance().keychain_path() / (std::string(key_id) + ".enc");
    if (fs::exists(path)) fs::remove(path);
}

// ── Channel system ────────────────────────────────────────────────
void System::register_channel(const ChannelDriver& driver) {
    m_channels.push_back(driver);
}

// start_channels() and stop_channels() are implemented in channel.cpp

// ── Project management ────────────────────────────────────────────
void System::set_project_dir(const fs::path& project_dir) {
    auto& cfg = Config::instance();
    cfg.set_project_dir(project_dir);
    auto proj_path = fs::absolute(project_dir).string();

    auto& recent = cfg.recent_projects;
    recent.erase(std::remove(recent.begin(), recent.end(), proj_path), recent.end());
    recent.insert(recent.begin(), proj_path);
    while (recent.size() > static_cast<size_t>(cfg.max_recent_projects)) {
        recent.pop_back();
    }

    cfg.save();
}

fs::path System::project_dir() const {
    return m_opts.project_dir.empty() ? fs::current_path() : fs::path(m_opts.project_dir);
}

std::vector<std::string> System::recent_projects() {
    return Config::instance().recent_projects;
}

nlohmann::json System::system_info() const {
    json info;
    info["version"] = AGENT_VERSION;
    info["agent_dir"] = Config::instance().agent_dir().string();

    auto& registry = ModuleRegistry::instance();
    json modules = json::array();
    for (auto* m : registry.all_modules()) {
        modules.push_back({
            {"name", m->name()},
            {"active", m->is_active()}
        });
    }
    info["modules"] = modules;
    return info;
}

} // namespace agent
