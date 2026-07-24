#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include <nlohmann/json.hpp>

namespace agent {

struct SessionMessage {
    std::string role;
    std::string content;
    int64_t timestamp;
    std::string tool_call_id;
    std::string tool_name;
    nlohmann::json metadata;
};

struct SessionInfo {
    std::string id;
    std::string name;
    std::string project_dir;
    int64_t created_at;
    int64_t updated_at;
    int message_count;
    int64_t total_tokens;
};

class SessionManager : public Module<SessionManager> {
public:
    static constexpr std::string_view static_name() { return "session"; }

    void on_initialize() override;
    void on_shutdown() override;

    // ── Session CRUD ───────────────────────────────────────────
    SessionInfo new_session(std::string_view project_dir, std::string_view name = "");
    void delete_session(std::string_view session_id);
    void rename_session(std::string_view session_id, std::string_view new_name);
    std::vector<SessionInfo> list_sessions(std::string_view project_dir);
    std::vector<SessionInfo> search_sessions(std::string_view query);
    std::optional<SessionInfo> get_session(std::string_view session_id);

    // ── Session selection ──────────────────────────────────────
    void set_current(std::string_view session_id);
    SessionInfo current_session();
    bool has_current() const;

    // ── Message management ─────────────────────────────────────
    void add_message(const SessionMessage& msg);
    std::vector<SessionMessage> get_messages(std::string_view session_id, size_t max_count = 0);
    std::vector<SessionMessage> get_messages_until(std::string_view session_id, int64_t until_timestamp);
    void clear_messages(std::string_view session_id);
    void truncate_messages(std::string_view session_id, size_t keep_count);
    size_t message_count(std::string_view session_id);

    // ── Media asset management ─────────────────────────────────
    void save_asset(std::string_view session_id, std::string_view base64_data, std::string_view mime_type);
    std::string get_asset(std::string_view asset_ref);  // #N -> base64
    void resolve_assets(std::vector<SessionMessage>& messages);
    void strip_assets(std::vector<SessionMessage>& messages);

    // ── Serialization ──────────────────────────────────────────
    void save_session(std::string_view session_id);
    void load_session(std::string_view session_id);

    // ── Context tracking ───────────────────────────────────────
    int64_t estimate_tokens(std::string_view session_id);
    double context_usage_percent(std::string_view session_id);

private:
    std::string m_current_id;
    std::map<std::string, std::vector<SessionMessage>> m_messages;  // session_id -> messages
    std::map<std::string, std::vector<std::string>> m_assets;       // session_id -> [#N -> base64]

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace agent
