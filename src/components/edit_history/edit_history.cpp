#include "components/edit_history/edit_history.hpp"
#include "core/config.hpp"
#include "core/logger.hpp"
#include "utils/fs.hpp"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

void EditHistory::on_initialize() {}
void EditHistory::on_shutdown() {
    save();
}

fs::path EditHistory::get_storage_path() const {
    return Config::instance().edit_history_dir() / "history.json";
}

void EditHistory::record(const EditRecord& record) {
    m_records.push_back(record);
    save();
}

void EditHistory::record_edit(std::string_view tool_name, const fs::path& file_path,
                               std::string_view before, std::string_view after) {
    EditRecord rec;
    rec.tool_name = tool_name;
    rec.file_path = file_path.string();
    rec.before = {{"content", before}};
    rec.after = {{"content", after}};

    // Simple diff
    std::string diff;
    auto lines_before = [](std::string_view s) {
        std::vector<std::string> lines;
        std::stringstream ss{std::string(s)};
        std::string line;
        while (std::getline(ss, line)) lines.push_back(line);
        return lines;
    };
    auto lb = lines_before(before);
    auto la = lines_before(after);

    // Generate simple diff
    size_t i = 0, j = 0;
    while (i < lb.size() && j < la.size()) {
        if (lb[i] == la[j]) {
            diff += "  " + lb[i] + "\n";
            i++; j++;
        } else {
            diff += "- " + lb[i] + "\n";
            i++;
        }
    }
    while (i < lb.size()) { diff += "- " + lb[i] + "\n"; i++; }
    while (j < la.size()) { diff += "+ " + la[j] + "\n"; j++; }

    rec.diff = diff;
    rec.timestamp = std::chrono::system_clock::now().time_since_epoch().count();

    record(rec);
}

std::vector<EditRecord> EditHistory::list_records(size_t limit) const {
    if (limit == 0 || limit >= m_records.size()) return m_records;
    return {m_records.end() - limit, m_records.end()};
}

std::vector<EditRecord> EditHistory::list_by_session(std::string_view session_id) const {
    std::vector<EditRecord> result;
    for (auto& r : m_records) {
        if (r.session_id == session_id) result.push_back(r);
    }
    return result;
}

std::vector<EditRecord> EditHistory::list_by_file(std::string_view file_path) const {
    std::vector<EditRecord> result;
    for (auto& r : m_records) {
        if (r.file_path == file_path) result.push_back(r);
    }
    return result;
}

std::vector<EditRecord> EditHistory::list_by_tool(std::string_view tool_name) const {
    std::vector<EditRecord> result;
    for (auto& r : m_records) {
        if (r.tool_name == tool_name) result.push_back(r);
    }
    return result;
}

bool EditHistory::rollback_to_record(size_t record_index) {
    if (record_index >= m_records.size()) return false;

    auto& rec = m_records[record_index];
    if (rec.before.contains("content")) {
        try {
            fsutil::write_file(rec.file_path, rec.before["content"].get<std::string>());
            LOG_INFO("EditHistory", "Rolled back file: " + rec.file_path);
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("EditHistory", "Failed to rollback file: " + rec.file_path + " - " + e.what());
            return false;
        }
    }
    return false;
}

bool EditHistory::rollback_to_session_start(std::string_view session_id) {
    auto records = list_by_session(session_id);
    if (records.empty()) return false;

    // Rollback to the first record's "before" state
    return rollback_to_record(0);
}

void EditHistory::save() {
    auto path = get_storage_path();
    json j = json::array();
    for (auto& r : m_records) {
        json entry;
        entry["tool_name"] = r.tool_name;
        entry["file_path"] = r.file_path;
        entry["before"] = r.before;
        entry["after"] = r.after;
        entry["diff"] = r.diff;
        entry["timestamp"] = r.timestamp;
        entry["session_id"] = r.session_id;
        j.push_back(entry);
    }
    std::ofstream f(path);
    f << j.dump(2);
}

void EditHistory::load() {
    auto path = get_storage_path();
    if (!fs::exists(path)) return;

    try {
        std::ifstream f(path);
        if (!f.is_open() || f.peek() == std::ifstream::traits_type::eof()) return;

        json j;
        f >> j;

        if (!j.is_array()) return;

        for (auto& e : j) {
            EditRecord rec;
            rec.tool_name = e.value("tool_name", "");
            rec.file_path = e.value("file_path", "");
            rec.before = e.value("before", json{});
            rec.after = e.value("after", json{});
            rec.diff = e.value("diff", "");
            rec.timestamp = e.value("timestamp", 0LL);
            rec.session_id = e.value("session_id", "");
            m_records.push_back(rec);
        }
    } catch (const std::exception& e) {
        // Corrupted or invalid history file - start fresh
        m_records.clear();
    }
}

} // namespace agent
