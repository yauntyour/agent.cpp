#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <nlohmann/json.hpp>

namespace agent {

struct LSPCapabilities {
    bool hover = false;
    bool completion = false;
    bool definition = false;
    bool references = false;
    bool diagnostics = false;
    bool formatting = false;
    bool rename = false;
    bool code_action = false;
    bool signature_help = false;
};

struct LSPServerConfig {
    std::string id;
    std::string name;
    std::string language;
    std::string command;
    std::vector<std::string> args;
    std::string initialization_options;
    int port = 0;  // 0 = use stdio
    LSPCapabilities capabilities;
    bool auto_start = false;
};

struct LSPResult {
    bool success;
    std::string content;    // rendered markdown / text
    nlohmann::json raw;
};

class LSP : public Module<LSP> {
public:
    static constexpr std::string_view static_name() { return "lsp"; }

    void on_initialize() override;
    void on_shutdown() override;

    // ── Server management ──────────────────────────────────────
    void add_server(const LSPServerConfig& config);
    void remove_server(std::string_view id);
    void start_server(std::string_view id);
    void stop_server(std::string_view id);
    std::vector<LSPServerConfig> list_servers() const;

    // ── LSP operations ─────────────────────────────────────────
    LSPResult hover(std::string_view file_uri, int line, int character);
    LSPResult completion(std::string_view file_uri, int line, int character);
    LSPResult definition(std::string_view file_uri, int line, int character);
    LSPResult references(std::string_view file_uri, int line, int character);
    LSPResult diagnostics(std::string_view file_uri);
    LSPResult formatting(std::string_view file_uri);
    LSPResult rename(std::string_view file_uri, int line, int character, std::string_view new_name);
    LSPResult code_action(std::string_view file_uri, int line, int character);

private:
    std::map<std::string, LSPServerConfig> m_servers;
    std::map<std::string, std::string> m_server_for_language;  // language -> server_id

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace agent
