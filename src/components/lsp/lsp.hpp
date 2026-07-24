#pragma once
#include "core/module.hpp"
#include "components/service/service.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>

namespace agent {

struct LSPCapabilities {
    bool hover = true;
    bool completion = true;
    bool definition = true;
    bool references = true;
    bool diagnostics = true;
    bool formatting = true;
    bool rename = true;
    bool code_action = true;
    bool signature_help = true;
};

struct LSPServerConfig {
    std::string id;
    std::string name;
    std::string language;
    std::string command;
    std::vector<std::string> args;
    std::string initialization_options;
    int port = 0;
    LSPCapabilities capabilities;
    bool auto_start = false;
};

struct LSPResult {
    bool success;
    std::string content;
    nlohmann::json raw;
};

struct LSPDiagnostic {
    int line;
    int character;
    std::string message;
    std::string severity;
    std::string source;
};

class LSP : public Module<LSP> {
public:
    static constexpr std::string_view static_name() { return "lsp"; }

    void on_initialize();
    void on_shutdown();

    void add_server(const LSPServerConfig& config);
    void remove_server(std::string_view id);
    void start_server(std::string_view id);
    void stop_server(std::string_view id);
    std::vector<LSPServerConfig> list_servers() const;
    bool is_server_running(std::string_view id) const;

    void open_document(std::string_view file_uri, std::string_view content,
                        std::string_view language_id, int version = 1);
    void update_document(std::string_view file_uri, std::string_view content, int version);
    void close_document(std::string_view file_uri);

    LSPResult hover(std::string_view file_uri, int line, int character);
    LSPResult completion(std::string_view file_uri, int line, int character);
    LSPResult definition(std::string_view file_uri, int line, int character);
    LSPResult references(std::string_view file_uri, int line, int character);
    LSPResult diagnostics(std::string_view file_uri);
    LSPResult formatting(std::string_view file_uri);
    LSPResult rename(std::string_view file_uri, int line, int character, std::string_view new_name);
    LSPResult code_action(std::string_view file_uri, int line, int character);

private:
    struct ServerConnection {
        LSPServerConfig config;
        StdioProcess process;
        bool initialized = false;
        std::atomic<int> next_request_id{1};
        std::mutex io_mutex;
        std::map<std::string, std::vector<LSPDiagnostic>> diagnostics_cache;
    };

    bool send_lsp_message(ServerConnection& conn, const nlohmann::json& msg);
    nlohmann::json read_lsp_message(ServerConnection& conn, int timeout_ms = 30000);
    nlohmann::json send_request(ServerConnection& conn, std::string_view method,
                                  const nlohmann::json& params = {});
    void send_notification(ServerConnection& conn, std::string_view method,
                            const nlohmann::json& params = {});
    bool initialize_server(ServerConnection& conn);

    ServerConnection* find_server_for_file(std::string_view file_uri);

    std::map<std::string, std::unique_ptr<ServerConnection>> m_connections;
    std::map<std::string, std::string> m_server_for_language;
    std::mutex m_mutex;
};

} // namespace agent
