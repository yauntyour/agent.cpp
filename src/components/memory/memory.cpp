#include "components/memory/memory.hpp"
#include "core/config.hpp"
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace agent {

using json = nlohmann::json;

void Memory::on_initialize() {
    m_storage_dir = Config::instance().memory_dir().string();
    if (!fs::exists(m_storage_dir)) fs::create_directories(m_storage_dir);
    load();
}

void Memory::on_shutdown() {
    save();
}

MemoryEntry Memory::save_entry(const MemoryEntry& entry) {
    std::string id = entry.id.empty()
                         ? "mem_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count())
                         : entry.id;

    MemoryEntry saved = entry;
    saved.id = id;
    saved.updated_at = std::chrono::system_clock::now().time_since_epoch().count();
    m_entries[id] = saved;
    save();
    return saved;
}

void Memory::update_entry(const MemoryEntry& entry) {
    m_entries[entry.id] = entry;
    m_entries[entry.id].updated_at = std::chrono::system_clock::now().time_since_epoch().count();
    save();
}

void Memory::delete_entry(std::string_view id) {
    m_entries.erase(std::string(id));
    save();
}

std::optional<MemoryEntry> Memory::get_entry(std::string_view id) {
    auto it = m_entries.find(std::string(id));
    if (it == m_entries.end()) return std::nullopt;
    return it->second;
}

std::vector<MemoryEntry> Memory::search(const MemoryQuery& query) {
    std::vector<std::pair<double, MemoryEntry>> scored;

    for (auto& [id, entry] : m_entries) {
        double score = 0.0;

        // Keyword matching
        if (!query.keywords.empty()) {
            for (auto& kw : query.keywords) {
                for (auto& ek : entry.keywords) {
                    if (kw == ek) score += 1.0;
                }
                // Also search in title and content
                std::string kw_lower = kw;
                std::transform(kw_lower.begin(), kw_lower.end(), kw_lower.begin(), ::tolower);
                std::string title_lower = entry.title;
                std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(), ::tolower);
                std::string content_lower = entry.content;
                std::transform(content_lower.begin(), content_lower.end(), content_lower.begin(), ::tolower);
                if (title_lower.find(kw_lower) != std::string::npos) score += 0.5;
                if (content_lower.find(kw_lower) != std::string::npos) score += 0.3;
            }
        }

        // Text similarity
        if (!query.text.empty()) {
            score += calculate_similarity(query.text, entry.title) * 2.0;
            score += calculate_similarity(query.text, entry.content) * 1.0;
        }

        // Category filter
        if (!query.category.empty() && entry.category != query.category) {
            score = 0;
        }

        // Importance bonus
        score += entry.importance;

        if (score >= query.min_similarity) {
            scored.emplace_back(score, entry);
        }
    }

    std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) {
        return a.first > b.first;
    });

    std::vector<MemoryEntry> results;
    size_t count = std::min(scored.size(), static_cast<size_t>(query.max_results));
    for (size_t i = 0; i < count; ++i) {
        results.push_back(scored[i].second);
    }

    return results;
}

std::vector<MemoryEntry> Memory::search_keyword(std::string_view keyword) {
    MemoryQuery q;
    q.keywords = {std::string(keyword)};
    return search(q);
}

std::vector<MemoryEntry> Memory::search_text(std::string_view text, int max_results) {
    MemoryQuery q;
    q.text = text;
    q.max_results = max_results;
    return search(q);
}

std::vector<MemoryEntry> Memory::list_by_category(std::string_view category) {
    std::vector<MemoryEntry> results;
    for (auto& [id, entry] : m_entries) {
        if (entry.category == category) {
            results.push_back(entry);
        }
    }
    return results;
}

std::vector<MemoryEntry> Memory::list_all() {
    std::vector<MemoryEntry> results;
    results.reserve(m_entries.size());
    for (auto& [id, entry] : m_entries) {
        results.push_back(entry);
    }
    return results;
}

void Memory::auto_generate_memory(const std::vector<ChatMessage>& conversation) {
    // Simple heuristic: extract important information from conversation
    // A real implementation would use the LLM to summarize
    std::string combined;
    for (auto& msg : conversation) {
        if (msg.role == "user" || msg.role == "assistant") {
            combined += msg.content + " ";
        }
    }

    // Extract keywords (simple frequency-based)
    std::map<std::string, int> word_freq;
    std::string word;
    for (char c : combined) {
        if (std::isalnum(c)) {
            word += static_cast<char>(std::tolower(c));
        } else if (!word.empty()) {
            if (word.size() > 3) word_freq[word]++;
            word.clear();
        }
    }

    // Find top keywords
    std::vector<std::pair<int, std::string>> top_words;
    for (auto& [w, f] : word_freq) {
        top_words.emplace_back(f, w);
    }
    std::sort(top_words.begin(), top_words.end(), std::greater<>());

    std::vector<std::string> keywords;
    for (size_t i = 0; i < std::min(top_words.size(), size_t(10)); ++i) {
        keywords.push_back(top_words[i].second);
    }

    // Create memory entry
    MemoryEntry entry;
    entry.title = "Session summary";
    entry.content = combined.size() > 500 ? combined.substr(0, 500) + "..." : combined;
    entry.keywords = keywords;
    entry.category = "auto";
    entry.importance = 0.3;
    entry.created_at = entry.updated_at = std::chrono::system_clock::now().time_since_epoch().count();

    save_entry(entry);
}

double Memory::calculate_similarity(std::string_view a, std::string_view b) {
    std::string a_lower(a);
    std::string b_lower(b);
    std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(), ::tolower);
    std::transform(b_lower.begin(), b_lower.end(), b_lower.begin(), ::tolower);

    // Simple Jaccard similarity on word sets
    auto to_words = [](const std::string& s) -> std::set<std::string> {
        std::set<std::string> words;
        std::string word;
        for (char c : s) {
            if (std::isalnum(c)) {
                word += c;
            } else if (!word.empty()) {
                words.insert(word);
                word.clear();
            }
        }
        if (!word.empty()) words.insert(word);
        return words;
    };

    auto words_a = to_words(a_lower);
    auto words_b = to_words(b_lower);

    if (words_a.empty() && words_b.empty()) return 0.0;

    std::set<std::string> intersection;
    std::set<std::string> union_set = words_a;
    union_set.insert(words_b.begin(), words_b.end());

    for (auto& w : words_a) {
        if (words_b.count(w)) intersection.insert(w);
    }

    return static_cast<double>(intersection.size()) / union_set.size();
}

void Memory::save() {
    auto path = Config::instance().memory_dir() / "memory.json";

    json j = json::array();
    for (auto& [id, entry] : m_entries) {
        json e;
        e["id"] = entry.id;
        e["title"] = entry.title;
        e["content"] = entry.content;
        e["keywords"] = entry.keywords;
        e["category"] = entry.category;
        e["created_at"] = entry.created_at;
        e["updated_at"] = entry.updated_at;
        e["importance"] = entry.importance;
        j.push_back(e);
    }

    std::ofstream f(path);
    f << j.dump(2);
}

void Memory::load() {
    auto path = Config::instance().memory_dir() / "memory.json";
    if (!fs::exists(path)) return;

    std::ifstream f(path);
    json j;
    f >> j;

    for (auto& e : j) {
        MemoryEntry entry;
        entry.id = e.value("id", "");
        entry.title = e.value("title", "");
        entry.content = e.value("content", "");
        if (e.contains("keywords")) entry.keywords = e["keywords"].get<std::vector<std::string>>();
        entry.category = e.value("category", "general");
        entry.created_at = e.value("created_at", 0LL);
        entry.updated_at = e.value("updated_at", 0LL);
        entry.importance = e.value("importance", 0.5);
        m_entries[entry.id] = entry;
    }
}

} // namespace agent
