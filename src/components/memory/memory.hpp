#pragma once
#include "core/module.hpp"
#include "components/provider/provider.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

namespace agent {

struct MemoryEntry {
    std::string id;
    std::string title;
    std::string content;
    std::vector<std::string> keywords;
    std::string category;
    int64_t created_at;
    int64_t updated_at;
    double importance = 0.5;
};

struct MemoryQuery {
    std::string text;
    std::vector<std::string> keywords;
    std::string category;
    int max_results = 10;
    double min_similarity = 0.3;
};

class Memory : public Module<Memory> {
public:
    static constexpr std::string_view static_name() { return "memory"; }

    void on_initialize();
    void on_shutdown();

    // ── CRUD ───────────────────────────────────────────────────
    MemoryEntry save_entry(const MemoryEntry& entry);
    void update_entry(const MemoryEntry& entry);
    void delete_entry(std::string_view id);
    std::optional<MemoryEntry> get_entry(std::string_view id);

    // ── Search ─────────────────────────────────────────────────
    std::vector<MemoryEntry> search(const MemoryQuery& query);
    std::vector<MemoryEntry> search_keyword(std::string_view keyword);
    std::vector<MemoryEntry> search_text(std::string_view text, int max_results = 5);
    std::vector<MemoryEntry> list_by_category(std::string_view category);
    std::vector<MemoryEntry> list_all();

    // ── Automatic memory generation ────────────────────────────
    void auto_generate_memory(const std::vector<ChatMessage>& conversation);

    // ── Persistence ────────────────────────────────────────────
    void save();
    void load();

private:
    std::map<std::string, MemoryEntry> m_entries;
    std::string m_storage_dir;

    double calculate_similarity(std::string_view a, std::string_view b);

};

} // namespace agent
