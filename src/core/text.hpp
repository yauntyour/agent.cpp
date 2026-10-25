#pragma once
#include "core/common.hpp"

// 小写化（仅 ASCII，避免 locale 影响）
inline std::string to_lower_ascii(std::string s)
{
    for (auto &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// 去掉首尾空白
inline std::string trim_ascii(const std::string &s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos)
        return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}


inline std::vector<std::string_view> extractAllTags(std::string_view text, std::string tag)
{
    std::vector<std::string_view> results;
    size_t pos = 0;

    while (true)
    {
        size_t start_tag_pos = text.find("<" + tag + ">", pos);
        if (start_tag_pos == std::string_view::npos)
            break;

        size_t content_start = start_tag_pos + tag.size() + 2;
        size_t end_tag_pos = text.find("</" + tag + ">", content_start);
        if (end_tag_pos == std::string_view::npos)
            break;

        size_t content_length = end_tag_pos - content_start;
        results.push_back(text.substr(content_start, content_length));

        pos = end_tag_pos + tag.size() + 3;
    }

    return results;
}

inline std::pair<std::string_view, std::string_view> parseArgs(std::string_view input, char c = ':')
{
    size_t colonPos = input.find(c);
    if (colonPos == std::string_view::npos)
    {
        return {input, {}};
    }
    else
    {
        std::string_view name = input.substr(0, colonPos);
        std::string_view args = input.substr(colonPos + 1);
        return {name, args};
    }
}

inline std::vector<std::string> get_all_files(const std::string &root_dir)
{
    std::vector<std::string> files;
    if (!std::filesystem::exists(root_dir) || !std::filesystem::is_directory(root_dir))
        throw std::runtime_error("Directory does not exist: " + root_dir);

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root_dir))
    {
        if (entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    return files;
}

inline auto file_parse(const std::string &file_path)
{
    std::filesystem::path p(file_path);
    return std::make_pair(p.stem().string(), p.extension().string());
}

inline std::vector<std::string> splitString(const std::string &s, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter))
        tokens.push_back(token);
    return tokens;
}

inline std::vector<std::string> splitEscaped(const std::string &s, char delimiter = '|')
{
    std::vector<std::string> tokens;
    std::string current;
    bool escape = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (escape)
        {
            current.push_back(c);
            escape = false;
        }
        else if (c == '\\')
            escape = true;
        else if (c == delimiter)
        {
            tokens.push_back(std::move(current));
            current.clear();
        }
        else
            current.push_back(c);
    }
    tokens.push_back(std::move(current));
    return tokens;
}
