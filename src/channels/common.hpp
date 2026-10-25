#pragma once
#include "core/common.hpp"
#include "core/text.hpp"
#include "core/crypto.hpp"
#include "core/config.hpp"
#include "net/http_client.hpp"
#include "agent/session.hpp"
#include "agent/fs_tools.hpp"
#include "agent/runtime.hpp"

namespace bot
{
    using json = nlohmann::json;

    // ---- 频道层公共能力来自 net / agent 模块 -------------------------------
    using net::HttpResp;
    using net::http_get;
    using net::http_post;
    using net::http_request;
    namespace http_impl = net::http_impl;

    // ════════════════════════════════════════════════════════════════
    // 工具函数
    // ════════════════════════════════════════════════════════════════

    inline std::string str_to_lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
                       { return (char)std::tolower(c); });
        return s;
    }

    inline std::string url_encode(const std::string &s)
    {
        std::ostringstream oss;
        oss << std::uppercase << std::hex;
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                oss << c;
            else
                oss << '%' << std::setw(2) << std::setfill('0') << (int)c;
        }
        return oss.str();
    }

    inline std::string rand_hex(size_t n)
    {
        static std::mt19937 rng(std::random_device{}());
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (size_t i = 0; i < n; ++i)
            oss << std::setw(2) << (int)(rng() % 256);
        return oss.str();
    }

    // ════════════════════════════════════════════════════════════════
    // AES-128-ECB（运行时生成 S-box，无外部依赖）
    // ════════════════════════════════════════════════════════════════

    // ════════════════════════════════════════════════════════════════
    // Markdown 安全分割（微信 2048 / Telegram 4096）
    // ════════════════════════════════════════════════════════════════
    inline bool is_word_char(unsigned char c) { return std::isalnum(c) || c >= 0x80; }

    inline std::vector<std::pair<size_t, size_t>> find_markdown_spans(const std::string &text)
    {
        std::vector<std::pair<size_t, size_t>> spans;
        std::smatch m;
        // std::regex 无 lookbehind，简化 \*[^*\n]+\* 与 _[^_\n]+_
        std::regex re(R"(```[\s\S]*?```|`[^`\n]+`|!?\[.*?\]\(.*?\)|\*\*.*?\*\*|__.*?__|\*[^*\n]+\*|_[^_\n]+_)");
        std::string::const_iterator it = text.begin(), end = text.end();
        while (std::regex_search(it, end, m, re))
        {
            size_t s = m.position() + (it - text.begin());
            spans.push_back({s, s + m.length()});
            it = m[0].second;
        }
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<size_t, size_t>> merged;
        for (auto &sp : spans)
        {
            if (!merged.empty() && sp.first <= merged.back().second)
                merged.back().second = std::max(merged.back().second, sp.second);
            else
                merged.push_back(sp);
        }
        return merged;
    }

    inline bool is_safe_split_pos(const std::string &text, size_t pos, const std::vector<std::pair<size_t, size_t>> &spans)
    {
        if (pos <= 0 || pos >= text.size())
            return false;
        for (auto &sp : spans)
            if (sp.first < pos && pos < sp.second)
                return false;
        if (pos < text.size() && is_word_char((unsigned char)text[pos]) && is_word_char((unsigned char)text[pos - 1]))
            return false;
        return true;
    }

    inline std::vector<std::string> split_long_paragraph_safe(const std::string &para, size_t max_len)
    {
        if (para.size() <= max_len)
            return {para};
        auto spans = find_markdown_spans(para);
        std::vector<std::string> parts;
        size_t start = 0;
        while (start < para.size())
        {
            size_t end = start + max_len;
            if (end >= para.size())
            {
                parts.push_back(para.substr(start));
                break;
            }
            size_t safe_pos = end;
            for (size_t candidate = end; candidate > start; --candidate)
                if (is_safe_split_pos(para, candidate, spans))
                {
                    if (candidate > 0 && std::string("。！？!?.").find(para[candidate - 1]) != std::string::npos)
                    {
                        safe_pos = candidate;
                        break;
                    }
                }
            if (safe_pos == end && !is_safe_split_pos(para, safe_pos, spans))
            {
                safe_pos = 0;
                for (size_t candidate = end; candidate > start; --candidate)
                    if (is_safe_split_pos(para, candidate, spans))
                    {
                        safe_pos = candidate;
                        break;
                    }
                if (safe_pos == 0)
                    safe_pos = start + max_len;
            }
            std::string part = para.substr(start, safe_pos - start);
            // rstrip
            size_t e = part.find_last_not_of(" \t\r\n");
            if (e == std::string::npos)
                part.clear();
            else
                part.resize(e + 1);
            if (!part.empty())
                parts.push_back(part);
            start = safe_pos;
            while (start < para.size() && para[start] == ' ')
                ++start;
        }
        return parts;
    }

    inline std::vector<std::string> split_markdown_text(const std::string &text, size_t max_len)
    {
        if (text.size() <= max_len)
            return {text};
        // 按段落分割
        std::vector<std::string> paragraphs;
        {
            std::string cur;
            size_t i = 0;
            while (i < text.size())
            {
                if (i + 1 < text.size() && text[i] == '\n' && text[i + 1] == '\n')
                {
                    paragraphs.push_back(cur);
                    cur.clear();
                    i += 2;
                }
                else
                {
                    cur += text[i];
                    ++i;
                }
            }
            paragraphs.push_back(cur);
        }
        std::vector<std::string> parts;
        std::string current;
        for (auto &para : paragraphs)
        {
            std::string candidate = current.empty() ? para : (current + "\n\n" + para);
            if (candidate.size() <= max_len)
            {
                current = candidate;
            }
            else
            {
                if (!current.empty())
                {
                    parts.push_back(current);
                    current.clear();
                }
                if (para.size() > max_len)
                {
                    auto sub = split_long_paragraph_safe(para, max_len);
                    parts.insert(parts.end(), sub.begin(), sub.end());
                }
                else
                {
                    current = para;
                }
            }
        }
        if (!current.empty())
            parts.push_back(current);
        // 最终兜底：强制按长度切割
        std::vector<std::string> final_parts;
        for (auto &p : parts)
        {
            if (p.size() > max_len)
                for (size_t i = 0; i < p.size(); i += max_len)
                    final_parts.push_back(p.substr(i, max_len));
            else
                final_parts.push_back(p);
        }
        return final_parts;
    }

    // ════════════════════════════════════════════════════════════════
    // 解析后端回复（与 Python 脚本一致）
    // ════════════════════════════════════════════════════════════════
    inline std::string extract_assistant_reply(const json &messages)
    {
        if (!messages.is_array())
            return "";
        for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        {
            std::string role = it->value("role", "");
            if (role == "user" || role == "tool")
                continue;
            const json &content = it->value("content", json());
            if (content.is_string())
                return content.get<std::string>();
            if (content.is_array())
            {
                std::string combined;
                for (auto &part : content)
                    if (part.value("type", "") == "text")
                        combined += part.value("text", "");
                if (!combined.empty())
                    return combined;
            }
        }
        return "";
    }

    inline std::vector<std::string> extract_images(const json &messages)
    {
        std::vector<std::string> images;
        if (!messages.is_array())
            return images;
        for (auto &msg : messages)
        {
            const json &content = msg.value("content", json());
            if (!content.is_array())
                continue;
            for (auto &part : content)
            {
                if (part.value("type", "") == "image_url")
                {
                    std::string url = part.value("image_url", json()).value("url", "");
                    if (!url.empty())
                        images.push_back(url);
                }
            }
        }
        return images;
    }

    // ════════════════════════════════════════════════════════════════
    // QR 二维码（SVG → base64）定义见文件末尾（qrcodegen 库之后）
    // ════════════════════════════════════════════════════════════════
    inline std::string qr_svg(const std::string &text);

    // ════════════════════════════════════════════════════════════════
    // 频道运行状态（供 webui 轮询）
    // ════════════════════════════════════════════════════════════════
    struct ChannelStatus
    {
        std::string name;
        bool running = false;
        std::string state = "idle"; // idle / waiting_qr / running / error / no_token
        std::string detail;
        std::string qr_svg_b64;
        std::string qr_url;
        std::string last_error;
    };

    inline std::mutex &status_mutex()
    {
        static std::mutex m;
        return m;
    }
    inline std::map<std::string, ChannelStatus> &status_map()
    {
        static std::map<std::string, ChannelStatus> m;
        return m;
    }

    inline ChannelStatus &channel_status(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        return status_map()[name];
    }

    inline void update_status(const std::string &name, bool running, const std::string &state,
                              const std::string &detail = "", const std::string &err = "")
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        auto &st = status_map()[name];
        st.name = name;
        st.running = running;
        st.state = state;
        st.detail = detail;
        if (!err.empty())
            st.last_error = err;
    }

    inline json channel_status_json()
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        json arr = json::array();
        for (auto &kv : status_map())
        {
            auto &s = kv.second;
            arr.push_back({{"name", s.name},
                           {"running", s.running},
                           {"state", s.state},
                           {"detail", s.detail},
                           {"last_error", s.last_error}});
        }
        return arr;
    }

    inline json channel_qr_json(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(status_mutex());
        auto it = status_map().find(name);
        if (it == status_map().end())
            return {{"name", name}, {"state", "idle"}, {"running", false}, {"detail", "频道未启动"}};
        auto &s = it->second;
        return {{"name", s.name},
                {"running", s.running},
                {"state", s.state},
                {"detail", s.detail},
                {"qr_svg", s.qr_svg_b64},
                {"qr_url", s.qr_url}};
    }

    // ════════════════════════════════════════════════════════════════
    // 停止标志
    //   worker 是阻塞式长轮询，没有取消点；这里用一个按名字的停止集合，
    //   循环每轮开头检查一次，收到请求后自然退出。
    // ════════════════════════════════════════════════════════════════
    inline std::set<std::string> &stop_set()
    {
        static std::set<std::string> s;
        return s;
    }
    inline std::mutex &stop_mutex()
    {
        static std::mutex m;
        return m;
    }
    inline bool stop_requested(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(stop_mutex());
        return stop_set().count(name) > 0;
    }
    inline void clear_stop(const std::string &name)
    {
        std::lock_guard<std::mutex> lk(stop_mutex());
        stop_set().erase(name);
    }
    inline void request_stop(const std::string &name)
    {
        {
            std::lock_guard<std::mutex> lk(stop_mutex());
            stop_set().insert(name);
        }
        update_status(name, false, "stopping", "正在停止（等待当前轮询返回）");
    }

    // ════════════════════════════════════════════════════════════════
    // Token 读写（workspace/tokens/<name>.enc，进程内加密/解密）
    // ════════════════════════════════════════════════════════════════
    inline std::string token_dir()
    {
        return run_unit::settings.value("workspace", ".") + "/tokens";
    }

    inline std::string load_channel_token(const std::string &name)
    {
        std::string file_path = token_dir() + "/" + name + ".enc";
        if (!std::filesystem::exists(file_path))
            return "";
        std::string encrypted = tool_unit::readFile(file_path);
        if (!encrypted.empty() && encrypted.back() == '\n')
            encrypted.pop_back();
        return crypto_unit::decrypt(encrypted, crypto_context::key());
    }

    inline bool save_channel_token(const std::string &name, const std::string &token)
    {
        try
        {
            std::filesystem::create_directories(token_dir());
            std::string file_path = token_dir() + "/" + name + ".enc";
            if (token.empty())
            {
                if (std::filesystem::exists(file_path))
                    std::filesystem::remove(file_path);
                return true;
            }
            std::string encrypted = crypto_unit::encrypt(token, crypto_context::key());
            if (encrypted.empty())
                return false;
            tool_unit::writeFile(file_path, encrypted);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace bot
