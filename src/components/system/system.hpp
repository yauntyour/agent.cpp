#pragma once
#include "core/module.hpp"
#include "core/config.hpp"
#include "components/provider/provider.hpp"
#include "components/session/session.hpp"
#include "components/memory/memory.hpp"
#include "components/permission/permission.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

namespace agent {

struct SystemCommand {
    std::string name;
    std::string description;
    std::string usage;
    std::vector<std::string> aliases;
    std::function<void(const std::vector<std::string>& args)> handler;
};

class System : public Module<System> {
public:
    static constexpr std::string_view static_name() { return "system"; }

    void on_initialize();
    void on_shutdown();

    // ── Initialization ─────────────────────────────────────────
    enum class InitMode { Normal, Reset, Recovery };
    void init(InitMode mode = InitMode::Normal);
    void hot_reload();
    void restart();
    void shutdown_system();

    // ── CLI argument parsing ───────────────────────────────────
    struct CLIOptions {
        bool router_mode = false;   // --router: enable HTTP server
        std::string project_dir;
        std::string session_id;
        std::string command;
        bool reset_config = false;
        bool version = false;
        bool help = false;
    };

    CLIOptions parse_args(int argc, char* argv[]);

    // ── Command system ─────────────────────────────────────────
    void register_command(SystemCommand cmd);
    void execute_command(std::string_view line);
    std::vector<SystemCommand> list_commands() const;

    // ── Keychain (argon2id encrypted storage) ──────────────────
    void store_key(std::string_view key_id, std::string_view value, std::string_view password);
    std::optional<std::string> retrieve_key(std::string_view key_id, std::string_view password);
    void delete_key(std::string_view key_id);

    // ── Channel system ─────────────────────────────────────────
    struct ChannelDriver {
        std::string type;       // "telegram", "wechat", "discord"
        std::string name;
        using MessageCallback = std::function<void(std::string_view text, const nlohmann::json& media)>;
        MessageCallback on_message;
        std::function<void(std::string_view text)> send_message;
        std::function<void(std::string_view file_path, std::string_view mime_type)> send_media;
    };

    void register_channel(const ChannelDriver& driver);
    void start_channels();
    void stop_channels();

    // ── Project management ─────────────────────────────────────
    void set_project_dir(const fs::path& project_dir);
    fs::path project_dir() const;
    std::vector<std::string> recent_projects();

    // ── System info ────────────────────────────────────────────
    nlohmann::json system_info() const;

private:
    void setup_commands();
    void load_keychain();
    void save_keychain();

    CLIOptions m_opts;
    std::map<std::string, SystemCommand> m_commands;
    std::vector<ChannelDriver> m_channels;

};

} // namespace agent
