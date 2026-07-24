#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <filesystem>
#include <regex>
#include <functional>

namespace fs = std::filesystem;

namespace agent::fsutil {

struct FileEntry {
    fs::path path;
    bool is_directory;
    bool is_symlink;
    uintmax_t size;
    fs::file_time_type modified;
};

// ── File operations ───────────────────────────────────────────────
std::string read_file(const fs::path& path);
std::optional<std::string> read_file_safe(const fs::path& path, size_t max_size = 10 * 1024 * 1024);
void write_file(const fs::path& path, std::string_view content);
void append_file(const fs::path& path, std::string_view content);
void edit_file(const fs::path& path, size_t start_line, size_t end_line, std::string_view new_content);
void edit_file_replace(const fs::path& path, std::string_view old_str, std::string_view new_str);

// ── Directory operations ──────────────────────────────────────────
std::vector<FileEntry> list_directory(const fs::path& path);
bool is_directory(const fs::path& path);
bool is_file(const fs::path& path);
void create_directories(const fs::path& path);
void remove_file(const fs::path& path);
void remove_directory(const fs::path& path);

// ── Search operations ─────────────────────────────────────────────
struct SearchResult {
    fs::path file_path;
    size_t line_number;
    std::string line_content;
    std::string match;
};

struct SearchOptions {
    std::string pattern;             // regex or glob
    bool regex_mode = false;
    bool case_sensitive = true;
    bool recursive = true;
    std::vector<std::string> include_patterns;   // e.g. "*.cpp", "*.hpp"
    std::vector<std::string> exclude_patterns;   // e.g. "*.exe", "*.dll"
    bool respect_gitignore = true;
    std::string search_path = ".";
    int max_results = 1000;
};

std::vector<SearchResult> search_content(const SearchOptions& opts);
std::vector<fs::path> search_files(const std::string& pattern, const fs::path& root = ".", bool recursive = true);
std::vector<fs::path> glob_search(const std::string& glob_pattern, const fs::path& root = ".");

// ── Gitignore reader ──────────────────────────────────────────────
class GitignorePatterns {
public:
    void load(const fs::path& gitignore_path);
    bool is_ignored(const fs::path& relative_path) const;

private:
    std::vector<std::regex> m_patterns;
};

// ── File watcher ──────────────────────────────────────────────────
class FileWatcher {
public:
    using Callback = std::function<void(const fs::path&, std::string_view event_type)>;

    void watch(const fs::path& directory, Callback callback);
    void stop();

private:
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

// ── Sandboxed path resolution ─────────────────────────────────────
fs::path resolve_sandbox(const fs::path& workspace, const fs::path& relative_path);
bool is_path_safe(const fs::path& workspace, const fs::path& path);

} // namespace agent::fsutil
