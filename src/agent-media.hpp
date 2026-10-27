// agent-media.hpp — 附件与表情包：落地、清单、解析
// =============================================================================
// 一次对话里可能出现的东西不止文字：桌面粘贴的截图、拖进来的 PDF、IM 里发来的
// 图片 / 语音 / 视频 / 文件 / 表情包。它们在这层的命运是一样的：
//
//   字节 → characters/<角色>/uploads/<时间-随机-名> → 一行清单 → 进上下文
//
// 附件和表情包都只落在角色自己的目录里（没有全局 uploads / stickers 了）：
// 角色换掉、删掉就是整个目录一起走，不会在数据根里留下无主的字节。
//
// 清单是纯文本（`[attach:image] 名字 | 绝对路径`）。这样做的三个理由：
//   1. 会话是 JSONL，base64 塞进去会长得没法看；路径只有几十字节；
//   2. 模型要看内容时用 read 工具打开 —— read 本身就会把图片 / 音频转成
//      多模态块（cell::box::read_multimodal），不需要在这里重写一遍；
//   3. 换模型供应商时不会因为上下文里躺着别家的 image_url 块而报错。
//
// 清单行同时是给页面看的：history 里解析出来就是附件卡片。
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <sodium.h>

#include "agent-paths.hpp"

namespace app::media
{
    // 一个附件在内存里的样子（path 为空 = 还没落地，只有远端地址）
    struct item
    {
        std::string kind = "file"; // image | audio | video | file
        std::string name;          // 展示名（原文件名，已裁短）
        std::string mime;
        std::string path; // 绝对路径
        std::string url;  // 页面可取的地址（本地附件才有）
        long long size = 0;
    };

    inline constexpr size_t kMaxUpload = 48ull * 1024 * 1024;
    inline constexpr size_t kMaxFiles = 8; // 一条消息最多带几个附件

    // 附件目录就是角色目录下的 uploads/（agent-paths.hpp 是唯一的一份定义）
    inline std::filesystem::path uploads_dir(const std::string &pid)
    {
        return paths::uploads_dir(pid);
    }

    inline std::string lower(std::string s)
    {
        for (char &c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    }

    // 扩展名 → MIME。表格里没有的按二进制算（read 会走文本读取并拒绝 NUL）。
    inline std::string mime_of(std::string_view path)
    {
        std::string ext;
        if (auto dot = path.find_last_of('.'); dot != std::string_view::npos)
            ext = lower(std::string(path.substr(dot + 1)));
        static const struct
        {
            const char *ext, *mime;
        } kMap[] = {
            {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
            {"webp", "image/webp"}, {"gif", "image/gif"}, {"bmp", "image/bmp"},
            {"mp3", "audio/mpeg"}, {"wav", "audio/wav"}, {"ogg", "audio/ogg"},
            {"opus", "audio/opus"}, {"m4a", "audio/mp4"}, {"silk", "audio/silk"},
            {"flac", "audio/flac"}, {"amr", "audio/amr"},
            {"mp4", "video/mp4"}, {"mov", "video/quicktime"}, {"webm", "video/webm"},
            {"mkv", "video/x-matroska"},
            {"pdf", "application/pdf"}, {"txt", "text/plain"}, {"md", "text/markdown"},
            {"json", "application/json"}, {"csv", "text/csv"}, {"zip", "application/zip"},
        };
        for (auto &m : kMap)
            if (ext == m.ext)
                return m.mime;
        return "application/octet-stream";
    }

    // 文件类型：MIME 优先，其次看扩展名（IM 平台常常只给文件名）
    inline std::string kind_of(const std::string &mime, std::string_view name = {})
    {
        std::string m = lower(mime);
        if (m.rfind("image/", 0) == 0)
            return "image";
        if (m.rfind("audio/", 0) == 0)
            return "audio";
        if (m.rfind("video/", 0) == 0)
            return "video";
        if (m.empty() || m == "application/octet-stream")
        {
            std::string by_name = mime_of(name);
            if (by_name != "application/octet-stream")
                return kind_of(by_name);
        }
        return "file";
    }

    inline std::string ext_of(std::string_view name)
    {
        if (auto dot = name.find_last_of('.'); dot != std::string_view::npos && name.size() - dot <= 8)
            return lower(std::string(name.substr(dot + 1)));
        return {};
    }

    // 默认扩展名：MIME 知道就按 MIME，否则按原文件名，最后退到 .bin
    inline std::string ext_for(const std::string &mime, std::string_view name = {})
    {
        std::string e = ext_of(name);
        if (!e.empty())
            return e;
        std::string m = lower(mime);
        if (m == "image/png") return "png";
        if (m == "image/jpeg") return "jpg";
        if (m == "image/webp") return "webp";
        if (m == "image/gif") return "gif";
        if (m == "audio/mpeg") return "mp3";
        if (m == "audio/wav" || m == "audio/x-wav") return "wav";
        if (m == "audio/ogg") return "ogg";
        if (m == "audio/amr") return "amr";
        if (m == "audio/mp4") return "m4a";
        if (m == "video/mp4") return "mp4";
        if (m == "video/quicktime") return "mov";
        if (m == "application/pdf") return "pdf";
        if (m == "text/plain") return "txt";
        return "bin";
    }

    // 文件名只留安全字符：原名里的路径、引号、控制字符一律不进磁盘
    inline std::string safe_stem(std::string_view name, size_t cap = 48)
    {
        std::string stem;
        if (auto dot = name.find_last_of('.'); dot != std::string_view::npos)
            name = name.substr(0, dot);
        for (unsigned char c : name)
        {
            if (stem.size() >= cap)
                break;
            if (std::isalnum(c))
                stem.push_back((char)c);
            else if (c >= 0x80 || c == '-' || c == '_')
                stem.push_back((char)c); // UTF-8 汉字照抄（磁盘与界面都认）
        }
        while (!stem.empty() && stem.front() == '.')
            stem.erase(stem.begin());
        return stem.empty() ? std::string("file") : stem;
    }

    inline std::string human_size(long long n)
    {
        if (n < 1024)
            return std::format("{} B", n);
        if (n < 1024 * 1024)
            return std::format("{:.0f} KB", (double)n / 1024.0);
        return std::format("{:.1f} MB", (double)n / (1024.0 * 1024.0));
    }

    // 落盘。同名不覆盖：时间 + 四位随机 + 原名，冲突再加后缀。
    inline bool save(const std::string &pid, std::string_view name, const std::string &mime,
                     const std::string &bytes, item &out, std::string &err)
    {
        if (bytes.empty())
        {
            err = "空文件";
            return false;
        }
        if (bytes.size() > kMaxUpload)
        {
            err = std::format("文件太大（{}，上限 {}）", human_size((long long)bytes.size()),
                              human_size((long long)kMaxUpload));
            return false;
        }
        std::error_code ec;
        auto dir = uploads_dir(pid);
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            err = "无法创建附件目录：" + ec.message();
            return false;
        }
        const std::string ext = ext_for(mime, name);
        const std::string stem = safe_stem(name);
        const long long stamp = (long long)std::time(nullptr);
        for (int attempt = 0; attempt < 8; attempt++)
        {
            std::string file = std::format("{}-{:04x}-{}.{}", stamp, randombytes_random() & 0xffff, stem, ext);
            auto path = dir / file;
            if (std::filesystem::exists(path, ec))
                continue;
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f)
            {
                err = "无法写入附件";
                return false;
            }
            f.write(bytes.data(), (std::streamsize)bytes.size());
            if (!f)
            {
                err = "附件写入中断";
                return false;
            }
            f.close();
            out.kind = kind_of(mime, file);
            out.name = std::string(name).empty() ? file : std::string(name);
            out.mime = mime.empty() ? mime_of(file) : mime;
            out.path = path.string();
            out.url = "/api/uploads/" + pid + "/" + file;
            out.size = (long long)bytes.size();
            return true;
        }
        err = "附件重名，换一个名字再试";
        return false;
    }

    // 附件行：[attach:image] 名字 | 绝对路径
    inline std::string manifest_line(const item &it)
    {
        return std::format("[attach:{}] {} | {}", it.kind, it.name.empty() ? "file" : it.name, it.path);
    }
    inline std::string manifest(const std::vector<item> &items)
    {
        if (items.empty())
            return {};
        std::string out = "\n\n";
        for (auto &it : items)
            out += manifest_line(it) + "\n";
        return out;
    }
    // 给模型的一句提示：附件在磁盘上，要看内容得自己读
    inline std::string manifest_hint(size_t n)
    {
        if (n == 0)
            return {};
        return std::format("（对方发来了 {} 个附件。图片 / 音频可以用 read 工具打开对应路径看清内容，"
                           "其它类型也可以 read 读正文；不要凭空猜附件里有什么。）",
                           n);
    }

    inline bool manifest_prefix(std::string_view line)
    {
        return line.rfind("[attach:", 0) == 0;
    }

    // 反解一行；`|` 只在分隔符处出现一次（名字里的 `|` 会被 safe_stem 之外的原名带进来，
    // 所以从右边找）
    inline bool parse_line(std::string_view line, item &out)
    {
        if (!manifest_prefix(line))
            return false;
        auto close = line.find(']');
        if (close == std::string_view::npos)
            return false;
        std::string kind(line.substr(8, close - 8));
        std::string_view rest = line.substr(close + 1);
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
        auto bar = rest.rfind(" | ");
        if (bar == std::string_view::npos)
            return false;
        out.kind = kind.empty() ? "file" : kind;
        out.name = std::string(rest.substr(0, bar));
        out.path = std::string(rest.substr(bar + 3));
        std::error_code ec;
        out.size = (long long)std::filesystem::file_size(out.path, ec);
        if (ec)
            out.size = 0;
        out.mime = mime_of(out.path);
        return !out.path.empty();
    }

    inline std::vector<item> parse_manifest(const std::string &text)
    {
        std::vector<item> out;
        size_t i = 0;
        while (i < text.size())
        {
            size_t nl = text.find('\n', i);
            std::string_view line(text.data() + i, (nl == std::string::npos ? text.size() : nl) - i);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            item it;
            if (parse_line(line, it))
                out.push_back(std::move(it));
            if (nl == std::string::npos)
                break;
            i = nl + 1;
        }
        return out;
    }

    // 页面展示用：去掉清单行和那句提示，只留人说的话
    inline std::string strip_manifest(const std::string &text)
    {
        std::string out;
        size_t i = 0;
        while (i < text.size())
        {
            size_t nl = text.find('\n', i);
            std::string_view line(text.data() + i, (nl == std::string::npos ? text.size() : nl) - i);
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            bool drop = manifest_prefix(line);
            if (!drop && line.rfind("（对方发来了 ", 0) == 0 && line.find("个附件") != std::string_view::npos)
                drop = true;
            // 语音转写的提示同样是给模型看的元信息，历史里不展示
            if (!drop && line.rfind("（语音", 0) == 0)
                drop = true;
            if (!drop)
            {
                out.append(line);
                out.push_back('\n');
            }
            if (nl == std::string::npos)
                break;
            i = nl + 1;
        }
        return cell::text::trim(out);
    }

    // ---- 表情包 --------------------------------------------------------
    // 目录里放图（png / jpg / gif / webp），文件名（去扩展名）就是表情包的名字。
    // 只有角色自己的 characters/<角色>/stickers/，没有全局目录了。
    inline std::vector<std::filesystem::path> sticker_dirs(const std::string &pid)
    {
        std::vector<std::filesystem::path> out;
        if (!pid.empty())
            out.push_back(paths::stickers_dir(pid));
        return out;
    }

    inline bool is_sticker_ext(const std::string &e)
    {
        return e == "png" || e == "jpg" || e == "jpeg" || e == "gif" || e == "webp";
    }

    inline std::vector<std::string> sticker_names(const std::string &pid)
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (auto &dir : sticker_dirs(pid))
        {
            if (!std::filesystem::is_directory(dir, ec))
                continue;
            for (auto &e : std::filesystem::directory_iterator(dir, ec))
            {
                if (ec || !e.is_regular_file(ec))
                    continue;
                if (!is_sticker_ext(lower(e.path().extension().string().substr(1))))
                    continue;
                std::string name = e.path().stem().string();
                if (name.empty() || name.size() > 48)
                    continue;
                if (std::find(out.begin(), out.end(), name) == out.end())
                    out.push_back(std::move(name));
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    inline std::string sticker_path(const std::string &pid, const std::string &name)
    {
        if (name.empty() || name.find('/') != std::string::npos || name.find('\\') != std::string::npos ||
            name.find("..") != std::string::npos)
            return {};
        std::error_code ec;
        for (auto &dir : sticker_dirs(pid))
        {
            for (const char *ext : {".png", ".jpg", ".jpeg", ".gif", ".webp"})
            {
                auto p = dir / (name + ext);
                if (std::filesystem::is_regular_file(p, ec))
                    return p.string();
            }
        }
        return {};
    }

    // ---- 页面地址 / 表情包写盘 -----------------------------------------
    // 表情包名字会出现在 URL 里：按 RFC 3986 的 unreserved 集合转义，
    // 与 agent-http 的 percent_decode 对称（中文名字也能取回）。
    inline std::string url_encode(std::string_view s)
    {
        static const char *hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() + 8);
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                out.push_back((char)c);
            else
            {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
        }
        return out;
    }

    inline std::string sticker_url(const std::string &pid, const std::string &name)
    {
        if (pid.empty() || name.empty())
            return {};
        return "/api/stickers/" + pid + "/" + url_encode(name);
    }

    // 附件绝对路径 → 页面地址；不在 characters/<角色>/uploads/ 下的（比如表情包）
    // 返回空。路径必须正好是 characters 根下面三层：<角色>/uploads/<文件>。
    // history_json 与 IM 清单都靠这一条决定 attachments[].url。
    inline std::string url_for_path(const std::string &path)
    {
        if (path.empty())
            return {};
        std::error_code ec;
        auto file = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
        if (ec || file.empty())
            return {};
        auto root = std::filesystem::weakly_canonical(paths::characters_dir(), ec);
        if (ec || root.empty())
            return {};
        auto rel = file.lexically_relative(root);
        if (rel.empty() || rel == ".")
            return {};
        auto it = rel.begin();
        if (it->string() == "..")
            return {};
        std::string pid = it->string();
        ++it;
        if (it == rel.end() || it->string() != "uploads")
            return {};
        ++it;
        if (it == rel.end())
            return {};
        std::string fname = it->string();
        ++it;
        if (it != rel.end())
            return {};
        return "/api/uploads/" + pid + "/" + url_encode(fname);
    }

    inline bool sticker_name_ok(const std::string &name)
    {
        if (name.empty() || name.size() > 48 || name.front() == '.')
            return false;
        if (name.find("..") != std::string::npos)
            return false;
        for (unsigned char c : name)
            if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                c == '"' || c == '<' || c == '>' || c == '|')
                return false;
        return true;
    }

    // 新增/覆盖一个表情包：写进角色自己的 stickers 目录（不存在就建）。
    // 同名换扩展名时先把旧文件删掉，免得 sticker_path 先撞上旧的那张。
    inline bool sticker_add(const std::string &pid, const std::string &name, std::string ext,
                            const std::string &bytes, item &out, std::string &err)
    {
        if (!sticker_name_ok(name))
        {
            err = "表情包名字不合法（最长 48 字节，不能含路径符号）";
            return false;
        }
        if (bytes.empty())
        {
            err = "空图片";
            return false;
        }
        if (bytes.size() > kMaxUpload)
        {
            err = std::format("表情包太大（{}，上限 {}）", human_size((long long)bytes.size()),
                              human_size((long long)kMaxUpload));
            return false;
        }
        ext = lower(std::move(ext));
        if (!is_sticker_ext(ext))
        {
            err = "只支持 png / jpg / jpeg / gif / webp";
            return false;
        }
        std::error_code ec;
        auto dir = paths::stickers_dir(pid);
        std::filesystem::create_directories(dir, ec);
        if (ec)
        {
            err = "无法创建表情包目录：" + ec.message();
            return false;
        }
        for (const char *old : {".png", ".jpg", ".jpeg", ".gif", ".webp"})
            if (ext != std::string(old + 1))
                std::filesystem::remove(dir / (name + old), ec);
        auto path = dir / (name + "." + ext);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
        {
            err = "无法写入表情包";
            return false;
        }
        f.write(bytes.data(), (std::streamsize)bytes.size());
        if (!f)
        {
            err = "表情包写入中断";
            return false;
        }
        f.close();
        out.kind = "sticker";
        out.name = name;
        out.mime = mime_of(path.string());
        out.path = path.string();
        out.url = sticker_url(pid, name);
        out.size = (long long)bytes.size();
        return true;
    }

    // 删掉一个表情包（在角色自己的 stickers 目录里找）。
    inline bool sticker_remove(const std::string &pid, const std::string &name, std::string &err)
    {
        if (!sticker_name_ok(name))
        {
            err = "表情包名字不合法";
            return false;
        }
        std::error_code ec;
        bool removed = false;
        for (auto &dir : sticker_dirs(pid))
            for (const char *ext : {".png", ".jpg", ".jpeg", ".gif", ".webp"})
            {
                auto p = dir / (name + ext);
                if (std::filesystem::is_regular_file(p, ec) && std::filesystem::remove(p, ec))
                    removed = true;
            }
        if (!removed)
            err = "没有这个表情包：" + name;
        return removed;
    }
} // namespace app::media
