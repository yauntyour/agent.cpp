#include "server/webui.hpp"

#include "core/base64.hpp"
#include "core/log.hpp"
#include "server/http_server.hpp"
#include "server/webui_assets.hpp"

namespace webui
{
    namespace
    {
        std::string to_lower(std::string s)
        {
            for (auto &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        // 从原始请求里取出路径（去掉 query）
        std::string request_path(const std::string &raw)
        {
            std::string url = http::extract_url(raw);
            size_t q = url.find('?');
            if (q != std::string::npos)
                url = url.substr(0, q);
            return url;
        }

        std::string url_decode(const std::string &s)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '%' && i + 2 < s.size())
                {
                    auto hex = [](char c) -> int
                    {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
                    if (hi >= 0 && lo >= 0)
                    {
                        out += static_cast<char>(hi * 16 + lo);
                        i += 2;
                        continue;
                    }
                }
                out += s[i];
            }
            return out;
        }

        // URL 路径 → 资源键（相对 WebUI 根的路径，统一用 '/'）
        std::string normalize_key(const std::string &url_path)
        {
            std::string rel = url_decode(url_path);
            size_t i = 0;
            while (i < rel.size() && rel[i] == '/')
                ++i;
            rel = rel.substr(i);

            std::vector<std::string> parts;
            std::string cur;
            auto flush = [&]()
            {
                if (cur == "..")
                {
                    if (!parts.empty())
                        parts.pop_back();
                }
                else if (!cur.empty() && cur != ".")
                    parts.push_back(cur);
                cur.clear();
            };
            for (char c : rel)
            {
                if (c == '/')
                    flush();
                else
                    cur += c;
            }
            flush();

            std::string out;
            for (size_t k = 0; k < parts.size(); ++k)
            {
                if (k)
                    out += '/';
                out += parts[k];
            }
            return out.empty() ? std::string(embedded::kIndex) : out;
        }

        const embedded::Asset *find_asset(const std::string &key)
        {
            for (size_t i = 0; i < embedded::kAssetCount; ++i)
                if (key == embedded::kAssets[i].path)
                    return &embedded::kAssets[i];
            return nullptr;
        }

        // 首次访问时解码 base64 分片并缓存
        const std::string &asset_body(const embedded::Asset *a)
        {
            static std::mutex m;
            static std::map<const embedded::Asset *, std::string> cache;
            std::lock_guard<std::mutex> lk(m);
            auto it = cache.find(a);
            if (it != cache.end())
                return it->second;
            std::string b64;
            b64.reserve(a->size * 4 / 3 + 8);
            for (const char *const *p = a->chunks; p && *p; ++p)
                b64 += *p;
            return cache.emplace(a, base64::from_base64(b64)).first->second;
        }

        std::string file_etag(const fs::path &p)
        {
            std::error_code ec;
            auto size = fs::file_size(p, ec);
            auto t = fs::last_write_time(p, ec);
            auto stamp = t.time_since_epoch().count();
            return "\"" + std::to_string(static_cast<unsigned long long>(stamp)) + "-" +
                   std::to_string(static_cast<unsigned long long>(size)) + "\"";
        }

        std::string asset_etag(const embedded::Asset *a)
        {
            return "\"embedded-" + std::to_string(static_cast<unsigned long long>(a->size)) + "\"";
        }

        bool read_all(const fs::path &p, std::string &out)
        {
            std::ifstream f(p, std::ios::binary);
            if (!f)
                return false;
            std::ostringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }

        bool disk_available()
        {
            std::error_code ec;
            return fs::exists(config::webui_path(), ec);
        }

        // webui.source: auto（默认，磁盘优先）/ disk / embedded
        bool use_embedded()
        {
            const std::string src = config::str_path("webui.source", "auto");
            if (src == "embedded")
                return true;
            if (src == "disk")
                return false;
            return !disk_available();
        }
    } // namespace

    std::string mime_of(const fs::path &p)
    {
        std::string ext = to_lower(p.extension().string());
        static const std::map<std::string, std::string> table = {
            {".html", "text/html"},
            {".htm", "text/html"},
            {".css", "text/css"},
            {".js", "application/javascript"},
            {".mjs", "application/javascript"},
            {".json", "application/json"},
            {".map", "application/json"},
            {".txt", "text/plain"},
            {".md", "text/markdown"},
            {".svg", "image/svg+xml"},
            {".png", "image/png"},
            {".jpg", "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif", "image/gif"},
            {".webp", "image/webp"},
            {".ico", "image/x-icon"},
            {".woff", "font/woff"},
            {".woff2", "font/woff2"},
            {".ttf", "font/ttf"},
            {".otf", "font/otf"},
            {".mp3", "audio/mpeg"},
            {".wav", "audio/wav"},
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".wasm", "application/wasm"},
            {".pdf", "application/pdf"},
        };
        auto it = table.find(ext);
        return it == table.end() ? "application/octet-stream" : it->second;
    }

    bool is_directory_mode()
    {
        std::error_code ec;
        return fs::is_directory(config::webui_path(), ec);
    }

    fs::path resolve_request_path(const std::string &url_path)
    {
        const fs::path root = config::webui_path();
        std::error_code ec;

        if (!fs::exists(root, ec))
            return {};

        // 单文件模式：任何路径都返回这个文件
        if (fs::is_regular_file(root, ec))
            return root;

        std::string rel = url_decode(url_path);
        size_t i = 0;
        while (i < rel.size() && rel[i] == '/')
            ++i;
        rel = rel.substr(i);
        if (rel.empty())
            rel = config::webui_index();

        fs::path candidate = (root / paths::from_utf8(rel)).lexically_normal();
        if (fs::is_directory(candidate, ec))
            candidate /= config::webui_index();

        if (!paths::is_within(candidate, root))
            return {};
        if (!fs::is_regular_file(candidate, ec))
            return {};
        return candidate;
    }

    // ------------------------------------------------------------------
    //  内嵌资源
    // ------------------------------------------------------------------
    std::string serve_embedded(const std::string &path)
    {
        const std::string key = normalize_key(path);
        const embedded::Asset *a = find_asset(key);
        if (!a)
            return http::build_response(404, "text/plain", "404 not found: " + path);

        const std::string &body = asset_body(a);
        std::vector<std::pair<std::string, std::string>> extra = {{"ETag", asset_etag(a)}};
        const std::string mime = a->mime;
        if (mime == "text/html" || mime == "application/javascript")
            extra.emplace_back("Cache-Control", "no-cache");
        else
            extra.emplace_back("Cache-Control", "public, max-age=3600");
        extra.emplace_back("X-WebUI-Source", "embedded");
        return http::build_response(200, mime, body, extra);
    }

    // ------------------------------------------------------------------
    //  统一入口
    // ------------------------------------------------------------------
    std::string serve(const std::string &raw_request)
    {
        const std::string path = request_path(raw_request);

        if (use_embedded())
            return serve_embedded(path);

        const fs::path file = resolve_request_path(path);
        if (file.empty())
        {
            const fs::path root = config::webui_path();
            std::error_code ec;
            if (!fs::exists(root, ec))
                return http::build_response(
                    500, "text/html",
                    "<h1>WebUI not found</h1><p>请检查 settings.json → webui.path（当前：<code>" +
                        paths::to_utf8(root) +
                        "</code>），或把 <code>webui.source</code> 设为 <code>embedded</code> 使用内嵌界面。</p>");
            return http::build_response(404, "text/plain", "404 not found: " + path);
        }

        // 单文件模式
        if (!is_directory_mode())
        {
            std::string body;
            if (!read_all(file, body))
                return http::build_response(500, "text/plain", "cannot read webui file");
            return http::build_response(200, "text/html", body, {{"X-WebUI-Source", "disk"}});
        }

        const std::string etag = file_etag(file);
        std::string req_lower = to_lower(raw_request.substr(0, std::min<size_t>(raw_request.size(), 4096)));
        if (req_lower.find("if-none-match: " + etag) != std::string::npos)
            return http::build_response(304, "text/plain", "", {{"ETag", etag}});

        std::string body;
        if (!read_all(file, body))
            return http::build_response(500, "text/plain", "cannot read " + paths::to_utf8(file));

        std::vector<std::pair<std::string, std::string>> extra = {{"ETag", etag}};
        const std::string mime = mime_of(file);
        if (mime == "text/html" || mime == "application/javascript")
            extra.emplace_back("Cache-Control", "no-cache");
        else
            extra.emplace_back("Cache-Control", "public, max-age=3600");
        extra.emplace_back("X-WebUI-Source", "disk");

        return http::build_response(200, mime, body, extra);
    }

    int handle(std::string &input, std::string &output,
               const std::map<std::string, std::string> &)
    {
        output = serve(input);
        return http::FLAG_DONE;
    }

    json info()
    {
        const fs::path root = config::webui_path();
        std::error_code ec;
        json j;
        j["path"] = paths::to_utf8(root);
        j["mode"] = is_directory_mode() ? "directory" : (fs::exists(root, ec) ? "file" : "missing");
        j["index"] = config::webui_index();
        j["exists"] = fs::exists(root, ec);
        j["source"] = use_embedded() ? "embedded" : "disk";
        j["embedded_files"] = static_cast<unsigned long long>(embedded::kAssetCount);

        json files = json::array();
        if (!use_embedded() && is_directory_mode())
        {
            int n = 0;
            for (auto &e : fs::recursive_directory_iterator(root, ec))
            {
                if (ec)
                    break;
                if (e.is_regular_file())
                {
                    files.push_back(paths::to_utf8(fs::relative(e.path(), root)));
                    if (++n >= 500)
                        break;
                }
            }
        }
        else
        {
            for (size_t i = 0; i < embedded::kAssetCount; ++i)
                files.push_back(embedded::kAssets[i].path);
        }
        j["files"] = files;
        return j;
    }
} // namespace webui
