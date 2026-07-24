#ifdef AGENT_ENABLE_LSP
#include "components/lsp/lsp.hpp"
#include "core/config.hpp"
#include <sstream>
#include <iostream>
#include <cstring>

namespace agent {

using json = nlohmann::json;

void LSP::on_initialize() {
    auto& cfg = Config::instance();
    for (auto& entry : cfg.lsp_servers) {
        LSPServerConfig sc;
        sc.id = entry.id;
        sc.language = entry.language;
        sc.command = entry.command;
        sc.args = entry.args;
        sc.auto_start = entry.auto_start;
        add_server(sc);
    }
}

void LSP::on_shutdown() {
    std::lock_guard lock(m_mutex);
    for (auto& [id, conn] : m_connections) {
        if (conn->process.is_running()) {
            send_notification(*conn, "shutdown");
            conn->process.close_stdin();
            conn->process.wait(2000);
            if (conn->process.is_running()) {
                conn->process.terminate();
            }
        }
    }
    m_connections.clear();
}

void LSP::add_server(const LSPServerConfig& config) {
    std::lock_guard lock(m_mutex);
    auto conn = std::make_unique<ServerConnection>();
    conn->config = config;
    m_connections[config.id] = std::move(conn);

    if (!config.language.empty()) {
        m_server_for_language[config.language] = config.id;
    }

    if (config.auto_start) {
        start_server(config.id);
    }
}

void LSP::remove_server(std::string_view id) {
    stop_server(id);
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(id));
    if (it != m_connections.end()) {
        for (auto sit = m_server_for_language.begin(); sit != m_server_for_language.end(); ) {
            if (sit->second == id) sit = m_server_for_language.erase(sit);
            else ++sit;
        }
        m_connections.erase(it);
    }
}

void LSP::start_server(std::string_view id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(id));
    if (it == m_connections.end()) return;

    auto& conn = *it->second;
    if (conn.process.is_running()) return;

    if (!conn.process.spawn(conn.config.command, conn.config.args)) {
        std::cerr << "LSP: Failed to spawn server " << id << std::endl;
        return;
    }

    if (!initialize_server(conn)) {
        std::cerr << "LSP: Failed to initialize server " << id << std::endl;
        conn.process.terminate();
        return;
    }
}

void LSP::stop_server(std::string_view id) {
    std::lock_guard lock(m_mutex);
    auto it = m_connections.find(std::string(id));
    if (it == m_connections.end()) return;

    auto& conn = *it->second;
    if (conn.process.is_running()) {
        send_notification(conn, "shutdown");
        conn.process.close_stdin();
        conn.process.wait(2000);
        if (conn.process.is_running()) {
            conn.process.terminate();
        }
    }
    conn.initialized = false;
}

std::vector<LSPServerConfig> LSP::list_servers() const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    std::vector<LSPServerConfig> servers;
    for (auto& [id, conn] : m_connections) {
        servers.push_back(conn->config);
    }
    return servers;
}

bool LSP::is_server_running(std::string_view id) const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    auto it = m_connections.find(std::string(id));
    if (it == m_connections.end()) return false;
    return it->second->process.is_running();
}

bool LSP::send_lsp_message(ServerConnection& conn, const json& msg) {
    std::string body = msg.dump();
    std::string header = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    return conn.process.write(header) && conn.process.write(body);
}

json LSP::read_lsp_message(ServerConnection& conn, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    std::string headers;
    while (std::chrono::steady_clock::now() < deadline) {
        std::string line = conn.process.read_line(1000);
        if (line.empty()) {
            if (!conn.process.is_running()) return nullptr;
            continue;
        }
        if (line == "\r" || line.empty()) {
            break;
        }
        headers += line + "\n";
    }

    size_t content_length = 0;
    std::istringstream hstream(headers);
    std::string hline;
    while (std::getline(hstream, hline)) {
        if (hline.find("Content-Length:") == 0 || hline.find("content-length:") == 0) {
            std::string val = hline.substr(hline.find(':') + 1);
            while (!val.empty() && (val.front() == ' ' || val.front() == '\r')) val.erase(0, 1);
            while (!val.empty() && (val.back() == '\r' || val.back() == '\n')) val.pop_back();
            try { content_length = std::stoul(val); } catch (...) { return nullptr; }
        }
    }

    if (content_length == 0) return nullptr;

    std::string body = conn.process.read_bytes(content_length, timeout_ms);
    if (body.size() != content_length) return nullptr;

    try {
        return json::parse(body);
    } catch (const json::parse_error&) {
        return nullptr;
    }
}

json LSP::send_request(ServerConnection& conn, std::string_view method,
                         const json& params) {
    std::lock_guard lock(conn.io_mutex);

    int id = conn.next_request_id++;

    json request = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        request["params"] = params;
    }

    if (!send_lsp_message(conn, request)) {
        return {{"error", {{"code", -32000}, {"message", "Failed to write"}}}};
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        json resp = read_lsp_message(conn, 5000);
        if (resp.is_null()) {
            if (!conn.process.is_running()) {
                return {{"error", {{"code", -32000}, {"message", "Server died"}}}};
            }
            continue;
        }

        if (resp.contains("method") && !resp.contains("id")) {
            if (resp["method"] == "textDocument/publishDiagnostics" &&
                resp.contains("params")) {
                auto& p = resp["params"];
                std::string uri = p.value("uri", "");
                std::vector<LSPDiagnostic> diags;
                if (p.contains("diagnostics")) {
                    for (auto& d : p["diagnostics"]) {
                        LSPDiagnostic diag;
                        diag.line = d["range"]["start"].value("line", 0);
                        diag.character = d["range"]["start"].value("character", 0);
                        diag.message = d.value("message", "");
                        int sev = d.value("severity", 1);
                        diag.severity = (sev == 1) ? "error" : (sev == 2) ? "warning" : "info";
                        diag.source = d.value("source", "");
                        diags.push_back(diag);
                    }
                }
                conn.diagnostics_cache[uri] = diags;
            }
            continue;
        }

        if (resp.contains("id") && resp["id"].get<int>() == id) {
            return resp;
        }
    }

    return {{"error", {{"code", -32000}, {"message", "Timeout"}}}};
}

void LSP::send_notification(ServerConnection& conn, std::string_view method,
                              const json& params) {
    std::lock_guard lock(conn.io_mutex);

    json notification = {
        {"jsonrpc", "2.0"},
        {"method", method}
    };
    if (!params.is_null() && !params.empty()) {
        notification["params"] = params;
    }

    send_lsp_message(conn, notification);
}

bool LSP::initialize_server(ServerConnection& conn) {
    json init_params = {
        {"processId", 0},
        {"rootUri", nullptr},
        {"capabilities", {
            {"textDocument", {
                {"hover", {{"contentFormat", json::array({"markdown", "plaintext"})}}},
                {"completion", {{"completionItem", {{"snippetSupport", false}}}}},
                {"definition", {{"linkSupport", false}}},
                {"references", {}},
                {"formatting", {}},
                {"rename", {}},
                {"codeAction", {{"codeActionLiteralSupport", {{"codeActionKind", {{"valueSet", json::array({"quickfix", "refactor", "source"})}}}}}}}
            }},
            {"workspace", {
                {"workspaceFolders", true}
            }}
        }}
    };

    if (!conn.config.initialization_options.empty()) {
        try {
            init_params["initializationOptions"] = json::parse(conn.config.initialization_options);
        } catch (...) {}
    }

    auto response = send_request(conn, "initialize", init_params);
    if (response.is_null() || response.contains("error")) {
        return false;
    }

    send_notification(conn, "initialized");
    conn.initialized = true;
    return true;
}

LSP::ServerConnection* LSP::find_server_for_file(std::string_view file_uri) {
    std::string ext;
    auto dot_pos = file_uri.rfind('.');
    if (dot_pos != std::string_view::npos) {
        ext = std::string(file_uri.substr(dot_pos + 1));
    }

    std::string lang;
    if (ext == "cpp" || ext == "cc" || ext == "cxx" || ext == "c") lang = "cpp";
    else if (ext == "hpp" || ext == "h" || ext == "hxx") lang = "cpp";
    else if (ext == "py") lang = "python";
    else if (ext == "js" || ext == "mjs") lang = "javascript";
    else if (ext == "ts" || ext == "tsx") lang = "typescript";
    else if (ext == "rs") lang = "rust";
    else if (ext == "go") lang = "go";
    else if (ext == "java") lang = "java";
    else if (ext == "cs") lang = "csharp";
    else lang = ext;

    auto it = m_server_for_language.find(lang);
    if (it != m_server_for_language.end()) {
        auto cit = m_connections.find(it->second);
        if (cit != m_connections.end() && cit->second->initialized) {
            return cit->second.get();
        }
    }

    for (auto& [id, conn] : m_connections) {
        if (conn->initialized && conn->process.is_running()) {
            return conn.get();
        }
    }

    return nullptr;
}

void LSP::open_document(std::string_view file_uri, std::string_view content,
                          std::string_view language_id, int version) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return;

    send_notification(*conn, "textDocument/didOpen", {
        {"textDocument", {
            {"uri", file_uri},
            {"languageId", language_id},
            {"version", version},
            {"text", content}
        }}
    });
}

void LSP::update_document(std::string_view file_uri, std::string_view content, int version) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return;

    send_notification(*conn, "textDocument/didChange", {
        {"textDocument", {{"uri", file_uri}, {"version", version}}},
        {"contentChanges", json::array({{{"text", content}}})}
    });
}

void LSP::close_document(std::string_view file_uri) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return;

    send_notification(*conn, "textDocument/didClose", {
        {"textDocument", {{"uri", file_uri}}}
    });
}

LSPResult LSP::hover(std::string_view file_uri, int line, int character) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/hover", {
        {"textDocument", {{"uri", file_uri}}},
        {"position", {{"line", line}, {"character", character}}}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    if (!response.contains("result") || response["result"].is_null()) return {true, "", response};

    auto& result = response["result"];
    std::string content;
    if (result.contains("contents")) {
        auto& c = result["contents"];
        if (c.is_string()) {
            content = c.get<std::string>();
        } else if (c.is_object() && c.contains("value")) {
            content = c["value"].get<std::string>();
        } else if (c.is_array()) {
            for (auto& item : c) {
                if (item.is_string()) content += item.get<std::string>() + "\n";
                else if (item.contains("value")) content += item["value"].get<std::string>() + "\n";
            }
        }
    }
    return {true, content, response};
}

LSPResult LSP::completion(std::string_view file_uri, int line, int character) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/completion", {
        {"textDocument", {{"uri", file_uri}}},
        {"position", {{"line", line}, {"character", character}}}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    if (!response.contains("result") || response["result"].is_null()) return {true, "[]", response};

    auto& result = response["result"];
    json items = result.is_array() ? result : result.value("items", json::array());

    std::string content;
    for (auto& item : items) {
        content += item.value("label", "") + " - " + item.value("detail", "") + "\n";
    }
    return {true, content, response};
}

LSPResult LSP::definition(std::string_view file_uri, int line, int character) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/definition", {
        {"textDocument", {{"uri", file_uri}}},
        {"position", {{"line", line}, {"character", character}}}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    if (!response.contains("result") || response["result"].is_null()) return {true, "", response};

    auto& result = response["result"];
    std::string content;
    if (result.is_array()) {
        for (auto& loc : result) {
            content += loc.value("uri", "") + ":" +
                       std::to_string(loc["range"]["start"].value("line", 0) + 1) + "\n";
        }
    } else if (result.contains("uri")) {
        content = result["uri"].get<std::string>() + ":" +
                  std::to_string(result["range"]["start"].value("line", 0) + 1);
    }
    return {true, content, response};
}

LSPResult LSP::references(std::string_view file_uri, int line, int character) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/references", {
        {"textDocument", {{"uri", file_uri}}},
        {"position", {{"line", line}, {"character", character}}},
        {"context", {{"includeDeclaration", true}}}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    if (!response.contains("result") || response["result"].is_null()) return {true, "", response};

    std::string content;
    for (auto& loc : response["result"]) {
        content += loc.value("uri", "") + ":" +
                   std::to_string(loc["range"]["start"].value("line", 0) + 1) + ":" +
                   std::to_string(loc["range"]["start"].value("character", 0) + 1) + "\n";
    }
    return {true, content, response};
}

LSPResult LSP::diagnostics(std::string_view file_uri) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto it = conn->diagnostics_cache.find(std::string(file_uri));
    if (it == conn->diagnostics_cache.end()) return {true, "No diagnostics", {}};

    std::string content;
    json arr = json::array();
    for (auto& d : it->second) {
        content += "[" + d.severity + "] " + d.message +
                   " (line " + std::to_string(d.line + 1) + ")\n";
        arr.push_back({
            {"line", d.line}, {"character", d.character},
            {"message", d.message}, {"severity", d.severity}
        });
    }
    return {true, content, arr};
}

LSPResult LSP::formatting(std::string_view file_uri) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/formatting", {
        {"textDocument", {{"uri", file_uri}}},
        {"options", {{"tabSize", 4}, {"insertSpaces", true}}}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    return {true, "Formatting applied", response};
}

LSPResult LSP::rename(std::string_view file_uri, int line, int character, std::string_view new_name) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/rename", {
        {"textDocument", {{"uri", file_uri}}},
        {"position", {{"line", line}, {"character", character}}},
        {"newName", new_name}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    return {true, "Rename applied", response};
}

LSPResult LSP::code_action(std::string_view file_uri, int line, int character) {
    std::lock_guard lock(m_mutex);
    auto* conn = find_server_for_file(file_uri);
    if (!conn) return {false, "No LSP server for this file", {}};

    auto response = send_request(*conn, "textDocument/codeAction", {
        {"textDocument", {{"uri", file_uri}}},
        {"range", {
            {"start", {{"line", line}, {"character", character}}},
            {"end", {{"line", line}, {"character", character}}}
        }},
        {"context", {{"diagnostics", json::array()}}}
    });

    if (response.contains("error")) return {false, response["error"]["message"], response};
    if (!response.contains("result") || response["result"].is_null()) return {true, "", response};

    std::string content;
    for (auto& action : response["result"]) {
        content += action.value("title", "") + "\n";
    }
    return {true, content, response};
}

} // namespace agent
#endif
