#include "components/session/session.hpp"
#include "core/config.hpp"
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

void SessionManager::on_initialize() {
    Config::instance().session_dir();
    Config::instance().assets_dir();
}

void SessionManager::on_shutdown() {
    if (!m_current_id.empty()) {
        save_session(m_current_id);
    }
}

SessionInfo SessionManager::new_session(std::string_view project_dir, std::string_view name) {
    auto now = std::chrono::system_clock::now().time_since_epoch().count();

    SessionInfo info;
    info.id = "session_" + std::to_string(now);
    info.name = name.empty() ? "Session " + std::to_string(now) : std::string(name);
    info.project_dir = project_dir;
    info.created_at = now;
    info.updated_at = now;
    info.message_count = 0;
    info.total_tokens = 0;

    m_current_id = info.id;
    m_messages[info.id] = {};
    m_assets[info.id] = {};

    save_session(info.id);
    return info;
}

void SessionManager::delete_session(std::string_view session_id) {
    auto id = std::string(session_id);
    m_messages.erase(id);
    m_assets.erase(id);

    auto session_path = Config::instance().session_dir() / (id + ".json");
    auto assets_path = Config::instance().session_dir() / "assets" / (id + "_assets.json");

    if (fs::exists(session_path)) fs::remove(session_path);
    if (fs::exists(assets_path)) fs::remove(assets_path);

    if (m_current_id == id) {
        m_current_id.clear();
    }
}

void SessionManager::rename_session(std::string_view session_id, std::string_view new_name) {
    auto id = std::string(session_id);
    // Rename is stored in-memory only; load/save handles it
}

std::vector<SessionInfo> SessionManager::list_sessions(std::string_view project_dir) {
    std::vector<SessionInfo> sessions;
    auto session_dir = Config::instance().session_dir();

    if (!fs::exists(session_dir)) return sessions;

    for (auto& entry : fs::directory_iterator(session_dir)) {
        if (entry.path().extension() != ".json") continue;
        if (entry.path().filename().string().find("_assets") != std::string::npos) continue;

        try {
            std::ifstream f(entry.path());
            json j;
            f >> j;

            SessionInfo info;
            info.id = j.value("id", "");
            info.name = j.value("name", "");
            info.project_dir = j.value("project_dir", "");
            info.created_at = j.value("created_at", 0LL);
            info.updated_at = j.value("updated_at", 0LL);
            info.message_count = j.value("message_count", 0);
            info.total_tokens = j.value("total_tokens", 0LL);
            sessions.push_back(info);
        } catch (...) {}
    }

    // Sort by updated_at descending
    std::sort(sessions.begin(), sessions.end(), [](auto& a, auto& b) {
        return a.updated_at > b.updated_at;
    });

    return sessions;
}

std::vector<SessionInfo> SessionManager::search_sessions(std::string_view query) {
    auto all = list_sessions("");
    std::vector<SessionInfo> results;
    std::string q(query);
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (auto& s : all) {
        std::string name_lower = s.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.find(q) != std::string::npos) {
            results.push_back(s);
        }
    }
    return results;
}

std::optional<SessionInfo> SessionManager::get_session(std::string_view session_id) {
    auto session_dir = Config::instance().session_dir();
    auto path = session_dir / (std::string(session_id) + ".json");
    if (!fs::exists(path)) return std::nullopt;

    std::ifstream f(path);
    json j;
    f >> j;

    SessionInfo info;
    info.id = j.value("id", "");
    info.name = j.value("name", "");
    info.project_dir = j.value("project_dir", "");
    info.created_at = j.value("created_at", 0LL);
    info.updated_at = j.value("updated_at", 0LL);
    info.message_count = j.value("message_count", 0);
    info.total_tokens = j.value("total_tokens", 0LL);
    return info;
}

void SessionManager::set_current(std::string_view session_id) {
    m_current_id = session_id;
    load_session(session_id);
}

SessionInfo SessionManager::current_session() {
    if (m_current_id.empty()) return {};
    auto info = get_session(m_current_id);
    if (!info) return {};
    return *info;
}

bool SessionManager::has_current() const {
    return !m_current_id.empty();
}

// ── Message management ────────────────────────────────────────────
void SessionManager::add_message(const SessionMessage& msg) {
    if (m_current_id.empty()) return;
    auto& msgs = m_messages[m_current_id];
    msgs.push_back(msg);

    // Update metadata
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    // Update session file periodically
    if (msgs.size() % 10 == 0) {
        save_session(m_current_id);
    }
}

std::vector<SessionMessage> SessionManager::get_messages(std::string_view session_id, size_t max_count) {
    auto id = std::string(session_id);
    auto it = m_messages.find(id);
    if (it == m_messages.end()) {
        load_session(id);
        it = m_messages.find(id);
    }
    if (it == m_messages.end()) return {};

    auto& msgs = it->second;
    // Resolve media assets
    resolve_assets(const_cast<std::vector<SessionMessage>&>(msgs));

    if (max_count == 0 || max_count >= msgs.size()) return msgs;
    return std::vector<SessionMessage>(msgs.end() - max_count, msgs.end());
}

std::vector<SessionMessage> SessionManager::get_messages_until(std::string_view session_id, int64_t until_timestamp) {
    auto msgs = get_messages(session_id);
    std::vector<SessionMessage> result;
    for (auto& m : msgs) {
        if (m.timestamp <= until_timestamp) result.push_back(m);
    }
    return result;
}

void SessionManager::clear_messages(std::string_view session_id) {
    auto id = std::string(session_id);
    m_messages[id].clear();
    m_assets[id].clear();
    save_session(id);
}

void SessionManager::truncate_messages(std::string_view session_id, size_t keep_count) {
    auto id = std::string(session_id);
    auto& msgs = m_messages[id];
    if (msgs.size() > keep_count) {
        msgs.erase(msgs.begin(), msgs.end() - keep_count);
    }
}

size_t SessionManager::message_count(std::string_view session_id) {
    auto it = m_messages.find(std::string(session_id));
    if (it == m_messages.end()) return 0;
    return it->second.size();
}

// ── Media asset management ────────────────────────────────────────
void SessionManager::save_asset(std::string_view session_id, std::string_view base64_data, std::string_view mime_type) {
    auto id = std::string(session_id);
    auto& assets = m_assets[id];
    size_t n = assets.size() + 1;
    assets.push_back(std::string(base64_data));
}

std::string SessionManager::get_asset(std::string_view asset_ref) {
    // asset_ref is like "#N"
    std::string ref(asset_ref);
    if (ref.empty() || ref[0] != '#') return "";
    try {
        int idx = std::stoi(ref.substr(1)) - 1;
        if (m_current_id.empty()) return "";
        auto& assets = m_assets[m_current_id];
        if (idx < 0 || idx >= static_cast<int>(assets.size())) return "";
        return assets[idx];
    } catch (...) {
        return "";
    }
}

void SessionManager::resolve_assets(std::vector<SessionMessage>& messages) {
    for (auto& msg : messages) {
        // Replace #N references with actual base64 data
        std::string& content = msg.content;
        size_t pos = 0;
        while ((pos = content.find("#", pos)) != std::string::npos) {
            size_t end = pos + 1;
            while (end < content.size() && std::isdigit(content[end])) end++;
            if (end > pos + 1) {
                std::string ref = content.substr(pos, end - pos);
                auto asset = get_asset(ref);
                if (!asset.empty()) {
                    content.replace(pos, ref.size(), "[Image: " + ref + "]");
                    // Store the actual data in metadata
                    msg.metadata["media_" + ref] = asset;
                }
            }
            pos = end;
        }
    }
}

void SessionManager::strip_assets(std::vector<SessionMessage>& messages) {
    for (size_t i = 0; i < messages.size(); ++i) {
        auto& msg = messages[i];
        // Find base64 data:image URIs and replace with #N references
        std::string& content = msg.content;
        size_t pos = 0;
        while ((pos = content.find("data:image/", pos)) != std::string::npos) {
            size_t end = content.find("\"", pos);
            if (end == std::string::npos) end = content.find(")", pos);
            if (end == std::string::npos) end = content.size();
            if (content[end - 1] == '"' || content[end - 1] == ')') end--;

            std::string data_uri = content.substr(pos, end - pos + 1);
            auto colon = data_uri.find("base64,");
            if (colon != std::string::npos) {
                std::string b64 = data_uri.substr(colon + 7);
                save_asset(m_current_id, b64, "");

                size_t idx = m_assets[m_current_id].size();
                content.replace(pos, data_uri.size(), "#" + std::to_string(idx));
            }
            pos = end + 1;
        }
    }
}

// ── Serialization ─────────────────────────────────────────────────
void SessionManager::save_session(std::string_view session_id) {
    auto id = std::string(session_id);
    auto& msgs = m_messages[id];
    auto& assets = m_assets[id];

    // Strip media first
    auto msgs_copy = msgs;
    strip_assets(msgs_copy);

    json j;
    j["id"] = id;
    j["message_count"] = msgs.size();

    json messages_array = json::array();
    for (auto& msg : msgs_copy) {
        json m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        m["timestamp"] = msg.timestamp;
        if (!msg.tool_call_id.empty()) m["tool_call_id"] = msg.tool_call_id;
        if (!msg.tool_name.empty()) m["tool_name"] = msg.tool_name;
        messages_array.push_back(m);
    }
    j["messages"] = messages_array;

    auto session_dir = Config::instance().session_dir();
    if (!fs::exists(session_dir)) fs::create_directories(session_dir);

    auto session_path = session_dir / (id + ".json");
    std::ofstream f(session_path);
    f << j.dump(2);

    // Save assets separately
    if (!assets.empty()) {
        auto assets_dir = session_dir / "assets";
        if (!fs::exists(assets_dir)) fs::create_directories(assets_dir);

        json assets_json = json::array();
        for (size_t i = 0; i < assets.size(); ++i) {
            json a;
            a["index"] = i + 1;
            a["data"] = assets[i];
            assets_json.push_back(a);
        }

        auto assets_path = assets_dir / (id + "_assets.json");
        std::ofstream af(assets_path);
        af << assets_json.dump();
    }
}

void SessionManager::load_session(std::string_view session_id) {
    auto id = std::string(session_id);
    auto session_path = Config::instance().session_dir() / (id + ".json");

    if (!fs::exists(session_path)) return;

    std::ifstream f(session_path);
    json j;
    f >> j;

    std::vector<SessionMessage> messages;
    if (j.contains("messages")) {
        for (auto& m : j["messages"]) {
            SessionMessage msg;
            msg.role = m.value("role", "user");
            msg.content = m.value("content", "");
            msg.timestamp = m.value("timestamp", 0LL);
            msg.tool_call_id = m.value("tool_call_id", "");
            msg.tool_name = m.value("tool_name", "");
            messages.push_back(msg);
        }
    }

    m_messages[id] = std::move(messages);

    // Load assets
    auto assets_path = Config::instance().session_dir() / "assets" / (id + "_assets.json");
    if (fs::exists(assets_path)) {
        std::ifstream af(assets_path);
        json aj;
        af >> aj;

        std::vector<std::string> assets;
        for (auto& a : aj) {
            if (a.contains("data")) {
                int idx = a["index"].get<int>() - 1;
                if (idx >= static_cast<int>(assets.size())) assets.resize(idx + 1);
                assets[idx] = a["data"].get<std::string>();
            }
        }
        m_assets[id] = std::move(assets);
    }
}

// ── Context tracking ──────────────────────────────────────────────
int64_t SessionManager::estimate_tokens(std::string_view session_id) {
    auto msgs = get_messages(session_id);
    int64_t total = 0;
    for (auto& msg : msgs) {
        total += msg.content.size() / 4 + 20; // Rough estimate
    }
    return total;
}

double SessionManager::context_usage_percent(std::string_view session_id) {
    auto& cfg = Config::instance();
    auto tokens = estimate_tokens(session_id);
    return static_cast<double>(tokens) / cfg.max_context_tokens * 100.0;
}

} // namespace agent
