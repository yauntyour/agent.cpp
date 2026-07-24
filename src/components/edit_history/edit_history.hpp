#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace agent {

struct EditRecord {
    std::string tool_name;
    std::string file_path;
    nlohmann::json before;      // file state before edit
    nlohmann::json after;       // file state after edit
    std::string diff;           // unified diff
    int64_t timestamp;
    std::string session_id;
};

class EditHistory : public Module<EditHistory> {
public:
    static constexpr std::string_view static_name() { return "edit_history"; }

    void on_initialize();
    void on_shutdown();

    // ── Record operations ──────────────────────────────────────
    void record(const EditRecord& record);
    void record_edit(std::string_view tool_name, const fs::path& file_path,
                     std::string_view before, std::string_view after);

    // ── Query operations ───────────────────────────────────────
    std::vector<EditRecord> list_records(size_t limit = 100) const;
    std::vector<EditRecord> list_by_session(std::string_view session_id) const;
    std::vector<EditRecord> list_by_file(std::string_view file_path) const;
    std::vector<EditRecord> list_by_tool(std::string_view tool_name) const;

    // ── Rollback ───────────────────────────────────────────────
    bool rollback_to_record(size_t record_index);
    bool rollback_to_session_start(std::string_view session_id);

    // ── Persistence ────────────────────────────────────────────
    void save();
    void load();

private:
    std::vector<EditRecord> m_records;
    fs::path m_project_dir;
    fs::path get_storage_path() const;

};

} // namespace agent
