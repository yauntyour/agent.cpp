#include "utils/fs.hpp"
#include "core/exception.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace fs = std::filesystem;

namespace agent::fsutil {

// ── File Operations ───────────────────────────────────────────────
std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        LOG_ERROR("FileSystem", "Cannot open file: " + path.string());
        throw FileSystemException(ErrorCode::FileReadFailed, "Cannot open file: " + path.string());
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

std::optional<std::string> read_file_safe(const fs::path& path, size_t max_size) {
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec || size > max_size) return std::nullopt;

    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;

    std::string content(size, '\0');
    f.read(content.data(), size);
    content.resize(f.gcount());
    return content;
}

void write_file(const fs::path& path, std::string_view content) {
    auto parent = path.parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        LOG_ERROR("FileSystem", "Cannot write file: " + path.string());
        throw FileSystemException(ErrorCode::FileWriteFailed, "Cannot write file: " + path.string());
    }
    f.write(content.data(), content.size());
}

void append_file(const fs::path& path, std::string_view content) {
    std::ofstream f(path, std::ios::binary | std::ios::app);
    if (!f) {
        LOG_ERROR("FileSystem", "Cannot append to file: " + path.string());
        throw FileSystemException(ErrorCode::FileWriteFailed, "Cannot append to file: " + path.string());
    }
    f.write(content.data(), content.size());
}

void edit_file(const fs::path& path, size_t start_line, size_t end_line, std::string_view new_content) {
    auto content = read_file(path);
    std::stringstream ss(content);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    if (start_line > lines.size()) start_line = lines.size();
    if (end_line > lines.size()) end_line = lines.size();
    if (start_line < 1) start_line = 1;
    if (end_line < start_line) end_line = start_line;

    // Split new_content into lines
    std::vector<std::string> new_lines;
    std::stringstream nss{std::string(new_content)};
    std::string new_line;
    while (std::getline(nss, new_line)) {
        new_lines.push_back(new_line);
    }

    // Replace lines [start_line-1, end_line) with new_lines
    lines.erase(lines.begin() + start_line - 1, lines.begin() + end_line);
    lines.insert(lines.begin() + start_line - 1, new_lines.begin(), new_lines.end());

    // Write back
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i < lines.size() - 1) result += '\n';
    }
    write_file(path, result);
}

void edit_file_replace(const fs::path& path, std::string_view old_str, std::string_view new_str) {
    std::string content = read_file(path);
    size_t pos = content.find(std::string(old_str));
    if (pos == std::string::npos) {
        LOG_ERROR("FileSystem", "Pattern not found in file: " + path.string());
        throw FileSystemException(ErrorCode::FileEditFailed, "Pattern not found in file: " + path.string());
    }
    content.replace(pos, old_str.size(), new_str);
    write_file(path, content);
}

// ── Directory Operations ──────────────────────────────────────────
std::vector<FileEntry> list_directory(const fs::path& path) {
    std::vector<FileEntry> entries;
    if (!fs::exists(path)) return entries;

    for (auto& entry : fs::directory_iterator(path)) {
        FileEntry fe;
        fe.path = entry.path();
        fe.is_directory = entry.is_directory();
        fe.is_symlink = entry.is_symlink();
        fe.size = entry.is_regular_file() ? entry.file_size() : 0;
        fe.modified = entry.last_write_time();
        entries.push_back(fe);
    }
    return entries;
}

bool is_directory(const fs::path& path) { return fs::is_directory(path); }
bool is_file(const fs::path& path) { return fs::is_regular_file(path); }

void create_directories(const fs::path& path) {
    fs::create_directories(path);
}

void remove_file(const fs::path& path) {
    fs::remove(path);
}

void remove_directory(const fs::path& path) {
    fs::remove_all(path);
}

// ── Search ────────────────────────────────────────────────────────
std::vector<SearchResult> search_content(const SearchOptions& opts) {
    std::vector<SearchResult> results;
    std::regex pattern;
    auto flags = std::regex_constants::ECMAScript;
    if (!opts.case_sensitive) flags |= std::regex_constants::icase;

    try {
        if (opts.regex_mode) {
            pattern = std::regex(opts.pattern, flags);
        }
    } catch (const std::regex_error&) {
        return results;
    }

    auto should_include = [&](const fs::path& p) -> bool {
        if (opts.include_patterns.empty()) return true;
        std::string ext = p.extension().string();
        std::string fname = p.filename().string();
        for (auto& inc : opts.include_patterns) {
            // Simple glob matching
            if (inc.starts_with("*.")) {
                if (ext == inc.substr(1)) return true;
            } else if (fname == inc) {
                return true;
            }
        }
        return false;
    };

    auto should_exclude = [&](const fs::path& p) -> bool {
        for (auto& exc : opts.exclude_patterns) {
            if (exc.starts_with("*.")) {
                if (p.extension().string() == exc.substr(1)) return true;
            }
        }
        return false;
    };

    std::function<void(const fs::path&)> search_dir = [&](const fs::path& dir) {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            if (results.size() >= static_cast<size_t>(opts.max_results)) return;
            auto& path = entry.path();

            if (entry.is_directory()) {
                if (opts.recursive) {
                    std::string dname = path.filename().string();
                    if (dname != ".git" && dname != "node_modules" && !dname.starts_with(".")) {
                        search_dir(path);
                    }
                }
            } else if (entry.is_regular_file() && should_include(path) && !should_exclude(path)) {
                auto content = read_file_safe(path, 10 * 1024 * 1024); // 10MB max per file
                if (!content) continue;

                std::vector<std::string> lines;
                std::stringstream ss(*content);
                std::string line;
                size_t line_num = 0;
                while (std::getline(ss, line) && results.size() < static_cast<size_t>(opts.max_results)) {
                    line_num++;
                    if (opts.regex_mode) {
                        if (std::regex_search(line, pattern)) {
                            results.push_back({path, line_num, line, line});
                        }
                    } else {
                        auto pos = opts.case_sensitive
                                       ? line.find(opts.pattern)
                                       : [&]() -> size_t {
                                           auto lower_line = line;
                                           std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
                                           auto lower_pat = opts.pattern;
                                           std::transform(lower_pat.begin(), lower_pat.end(), lower_pat.begin(), ::tolower);
                                           return lower_line.find(lower_pat);
                                         }();
                        if (pos != std::string::npos) {
                            results.push_back({path, line_num, line, opts.pattern});
                        }
                    }
                }
            }
        }
    };

    search_dir(opts.search_path);
    return results;
}

std::vector<fs::path> search_files(const std::string& pattern, const fs::path& root, bool recursive) {
    std::vector<fs::path> results;
    std::function<void(const fs::path&)> search = [&](const fs::path& dir) {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            auto& path = entry.path();
            std::string fname = path.filename().string();
            if (fname.find(pattern) != std::string::npos) {
                results.push_back(path);
            }
            if (entry.is_directory() && recursive) {
                search(path);
            }
        }
    };
    search(root);
    return results;
}

std::vector<fs::path> glob_search(const std::string& glob_pattern, const fs::path& root) {
    std::vector<fs::path> results;
    // Convert simple glob to regex: * -> .*, ? -> ., ** -> .*
    std::string regex_str;
    regex_str.reserve(glob_pattern.size() * 2);
    regex_str += '^';
    for (size_t i = 0; i < glob_pattern.size(); ++i) {
        switch (glob_pattern[i]) {
        case '*':
            if (i + 1 < glob_pattern.size() && glob_pattern[i + 1] == '*') {
                regex_str += ".*";
                ++i;
            } else {
                regex_str += "[^/]*";
            }
            break;
        case '?': regex_str += '.'; break;
        case '.': regex_str += "\\."; break;
        case '\\': regex_str += "\\\\"; break;
        case '/': regex_str += "\\/"; break;
        default: regex_str += glob_pattern[i];
        }
    }
    regex_str += '$';

    std::regex re(regex_str);
    std::function<void(const fs::path&)> walk = [&](const fs::path& dir) {
        std::error_code ec;
        for (auto& entry : fs::directory_iterator(dir, ec)) {
            std::string rel = fs::relative(entry.path(), root).string();
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (std::regex_match(rel, re)) {
                results.push_back(entry.path());
            }
            if (entry.is_directory()) {
                walk(entry.path());
            }
        }
    };
    walk(root);
    return results;
}

// ── Gitignore ─────────────────────────────────────────────────────
void GitignorePatterns::load(const fs::path& gitignore_path) {
    if (!fs::exists(gitignore_path)) return;

    std::ifstream f(gitignore_path);
    std::string line;
    while (std::getline(f, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') continue;

        // Convert gitignore pattern to regex
        std::string regex_str = "^";
        bool is_dir_only = line.back() == '/';
        if (is_dir_only) line.pop_back();

        bool anchored = line[0] != '/';
        if (line[0] == '/') line = line.substr(1);

        for (size_t i = 0; i < line.size(); ++i) {
            switch (line[i]) {
            case '*':
                if (i + 1 < line.size() && line[i + 1] == '*') {
                    regex_str += ".*";
                    ++i;
                } else {
                    regex_str += "[^/]*";
                }
                break;
            case '?': regex_str += "."; break;
            case '.': regex_str += "\\."; break;
            case '\\': regex_str += "\\\\"; break;
            default: regex_str += line[i];
            }
        }
        regex_str += "$";

        m_patterns.emplace_back(regex_str);
    }
}

bool GitignorePatterns::is_ignored(const fs::path& relative_path) const {
    std::string path_str = relative_path.string();
    std::replace(path_str.begin(), path_str.end(), '\\', '/');
    for (auto& re : m_patterns) {
        if (std::regex_match(path_str, re)) return true;
    }
    return false;
}

// ── Sandbox ───────────────────────────────────────────────────────
fs::path resolve_sandbox(const fs::path& workspace, const fs::path& relative_path) {
    auto resolved = fs::weakly_canonical(workspace / relative_path);
    auto canonical_ws = fs::weakly_canonical(workspace);
    if (!is_path_safe(workspace, resolved)) {
        LOG_ERROR("FileSystem", "Path escapes workspace sandbox: " + resolved.string());
        throw FileSystemException(ErrorCode::PathSandboxEscape, "Path escapes workspace sandbox: " + resolved.string());
    }
    return resolved;
}

bool is_path_safe(const fs::path& workspace, const fs::path& path) {
    auto canonical_ws = fs::weakly_canonical(workspace);
    auto canonical_path = fs::weakly_canonical(path);
    auto [ws_end, _] = std::mismatch(canonical_ws.begin(), canonical_ws.end(),
                                      canonical_path.begin(), canonical_path.end());
    return ws_end == canonical_ws.end();
}

} // namespace agent::fsutil
