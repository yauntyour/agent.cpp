#ifdef AGENT_ENABLE_LSP
#include "components/lsp/lsp.hpp"

namespace agent {

void LSP::on_initialize() {}
void LSP::on_shutdown() {
    for (auto& [id, config] : m_servers) {
        stop_server(id);
    }
}

void LSP::add_server(const LSPServerConfig& config) {
    m_servers[config.id] = config;
    if (!config.language.empty()) {
        m_server_for_language[config.language] = config.id;
    }
    if (config.auto_start) start_server(config.id);
}

void LSP::remove_server(std::string_view id) {
    stop_server(id);
    m_servers.erase(std::string(id));
    for (auto it = m_server_for_language.begin(); it != m_server_for_language.end(); ) {
        if (it->second == id) it = m_server_for_language.erase(it);
        else ++it;
    }
}

void LSP::start_server(std::string_view id) {
    auto& svc = ModuleRegistry::instance().require<Service>();
    auto it = m_servers.find(std::string(id));
    if (it == m_servers.end()) return;

    ServiceProcess proc;
    proc.name = "lsp-" + std::string(id);
    proc.command = it->second.command;
    // LSP communication uses stdio pipes
    // Full implementation requires pipe management for JSON-RPC
    svc.spawn(proc);
}

void LSP::stop_server(std::string_view id) {
    auto& svc = ModuleRegistry::instance().require<Service>();
    svc.stop("lsp-" + std::string(id));
}

std::vector<LSPServerConfig> LSP::list_servers() const {
    std::vector<LSPServerConfig> servers;
    for (auto& [id, cfg] : m_servers) {
        servers.push_back(cfg);
    }
    return servers;
}

// LSP operations — these use JSON-RPC over stdio to the LSP server
LSPResult LSP::hover(std::string_view file_uri, int line, int character) {
    return {false, "LSP hover not yet implemented", {}};
}
LSPResult LSP::completion(std::string_view file_uri, int line, int character) {
    return {false, "LSP completion not yet implemented", {}};
}
LSPResult LSP::definition(std::string_view file_uri, int line, int character) {
    return {false, "LSP definition not yet implemented", {}};
}
LSPResult LSP::references(std::string_view file_uri, int line, int character) {
    return {false, "LSP references not yet implemented", {}};
}
LSPResult LSP::diagnostics(std::string_view file_uri) {
    return {false, "LSP diagnostics not yet implemented", {}};
}
LSPResult LSP::formatting(std::string_view file_uri) {
    return {false, "LSP formatting not yet implemented", {}};
}
LSPResult LSP::rename(std::string_view file_uri, int line, int character, std::string_view new_name) {
    return {false, "LSP rename not yet implemented", {}};
}
LSPResult LSP::code_action(std::string_view file_uri, int line, int character) {
    return {false, "LSP code action not yet implemented", {}};
}

} // namespace agent
#endif
