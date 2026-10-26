// agent-http.hpp — 本机服务：界面、命令总线、事件流、音频/头像静态资源
// =============================================================================
// 与 cell-app 完全同一套做法：桌面窗口和浏览器拿到的是同一个页面、走同一条
// HTTP + SSE 通道，所以调试界面不需要重新编译，脚本也能直接驱动同一个角色。
//
//   GET  /                  界面（内嵌页面，或 --ui FILE 从磁盘读）
//   POST /api/cmd/<name>    命令总线（JSON 进，JSON 出）
//   GET  /api/state         状态快照
//   GET  /api/events        SSE：一条 data: {...} 一行
//   GET  /api/audio/<name>  TTS 产出的音频
//   GET  /api/avatar/<id>   角色头像
//   GET  /api/uploads/<pid>/<file>   聊天附件（图片 / 语音 / 视频 / 文件）
//   GET  /api/stickers/<pid>/<name>  表情包
//   GET  /api/live2d/<…>    舞台资源：运行时库与模型文件（.agent/live2d/ 下）
//
// 安全模型：服务是一个「能记事、能说话」的本机控制面，绝不能被网页或局域网
// 碰到。只绑 127.0.0.1；每个请求必须带 loopback Host、同源 Origin（或没有
// Origin）；跨站标记直接拒绝；没有 CORS。
#pragma once

#include "servic.hpp"
#include "agent-media.hpp" // 附件 / 贴纸的落盘路径与 MIME（路由要用）

#include <boost/asio.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

namespace app
{
    namespace asio = boost::asio;

    struct sse_conn
    {
        static constexpr size_t kMaxQueue = 4096;
        std::mutex mx;
        std::condition_variable cv;
        bool alive = true;
        std::deque<std::string> q;
        std::function<void(const std::string &)> write; // 断开时抛异常
    };

    struct http_service
    {
        app_core &core;
        asio::io_context io;
        std::unique_ptr<servic::Server> server;
        rt::router ros;
        std::vector<std::thread> threads;
        // io_context 必须从 run() 之前就“有活干”，否则第一个 run() 会立刻返回并把
        // context 标记为 stopped，之后的 run()（以及 accept 协程）全都起不来。
        std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work;
        std::string ui_path;
        int bound_port = 0;
        std::mutex conns_mx;
        std::vector<std::weak_ptr<sse_conn>> conns;

        explicit http_service(app_core &c) : core(c) {}
        ~http_service() { stop(); }

        // ---- helpers ------------------------------------------------------
        static std::string lower_ascii(std::string s)
        {
            for (char &c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }
        static std::string http_response(const std::string &status, const std::string &ctype,
                                         const std::string &body, const std::string &extra = "")
        {
            return "HTTP/1.1 " + status + "\r\nContent-Type: " + ctype +
                   "\r\nX-Content-Type-Options: nosniff\r\n" + extra +
                   std::format("Content-Length: {}\r\nConnection: close\r\n\r\n", body.size()) + body;
        }
        static std::string text_response(const std::string &status, const std::string &body)
        {
            return http_response(status, "text/plain; charset=utf-8", body);
        }
        static std::string http_json(const nlohmann::json &j)
        {
            return http_response("200 OK", "application/json; charset=utf-8",
                                 j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        }
        bool host_allowed(const std::string &host) const
        {
            std::string h = lower_ascii(host);
            std::string p = std::to_string(bound_port);
            return h == "127.0.0.1:" + p || h == "localhost:" + p || h == "[::1]:" + p;
        }
        bool origin_allowed(const std::string &origin) const
        {
            std::string o = lower_ascii(origin);
            const std::string scheme = "http://";
            if (o.rfind(scheme, 0) != 0)
                return false;
            return host_allowed(o.substr(scheme.size()));
        }
        std::string check_request(const std::string &raw, const std::string &want_method) const
        {
            if (servic::request_method(raw) != want_method)
                return text_response("405 Method Not Allowed", "method not allowed");
            if (!host_allowed(servic::header_value(raw, "Host")))
                return text_response("403 Forbidden", "loopback Host header required");
            std::string origin = servic::header_value(raw, "Origin");
            if (!origin.empty() && !origin_allowed(origin))
                return text_response("403 Forbidden", "cross-origin request refused");
            if (lower_ascii(servic::header_value(raw, "Sec-Fetch-Site")) == "cross-site")
                return text_response("403 Forbidden", "cross-site request refused");
            return "";
        }
        // 查询串（WeCom 回调的 msg_signature / timestamp / nonce / echostr 都在这）
        static std::string request_query(const std::string &raw)
        {
            size_t line_end = raw.find_first_of("\r\n");
            if (line_end == std::string::npos)
                return {};
            std::istringstream iss(raw.substr(0, line_end));
            std::string method, target, version;
            if (!(iss >> method >> target >> version))
                return {};
            size_t q = target.find('?');
            return q == std::string::npos ? std::string() : target.substr(q + 1);
        }
        static void parse_request(const std::string &raw, std::string &method, std::string &path, std::string &body)
        {
            method.clear();
            path.clear();
            body.clear();
            size_t line_end = raw.find_first_of("\r\n");
            if (line_end == std::string::npos)
                return;
            std::istringstream iss(raw.substr(0, line_end));
            std::string target, version;
            if (iss >> method >> target >> version)
            {
                size_t q = target.find('?');
                path = q == std::string::npos ? target : target.substr(0, q);
            }
            size_t hdr_end = raw.find("\r\n\r\n");
            if (hdr_end != std::string::npos)
                body = raw.substr(hdr_end + 4);
        }
        // 文件名白名单：只允许一层、只允许安全字符
        static bool plain_name_ok(const std::string &n)
        {
            if (n.empty() || n.size() > 128 || n.front() == '.')
                return false;
            for (unsigned char c : n)
                if (!(std::isalnum(c) || c == '.' || c == '-' || c == '_'))
                    return false;
            return n.find("..") == std::string::npos;
        }
        // 附件的落盘名允许 UTF-8（中文原名照抄），但仍必须是一层、不含路径/控制字符
        static bool upload_name_ok(const std::string &n)
        {
            if (n.empty() || n.size() > 192 || n.front() == '.')
                return false;
            if (n.find("..") != std::string::npos)
                return false;
            for (unsigned char c : n)
                if (c == '/' || c == '\\' || c == ':' || c < 0x20 || c == 0x7f)
                    return false;
            return true;
        }
        // 附件目录的 owner：角色 id 或 IM 渠道 id（"im-<chan>"）
        static bool upload_pid_ok(const std::string &s)
        {
            if (s.empty() || s.size() > 40)
                return false;
            for (unsigned char c : s)
                if (!(std::isalnum(c) || c == '-' || c == '_'))
                    return false;
            return true;
        }
        static std::string read_file(const std::filesystem::path &p)
        {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        }
        static std::string percent_decode(const std::string &s)
        {
            std::string out;
            out.reserve(s.size());
            for (size_t i = 0; i < s.size(); i++)
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
                        out.push_back((char)((hi << 4) | lo));
                        i += 2;
                        continue;
                    }
                }
                out.push_back(s[i]);
            }
            return out;
        }
        // 舞台资源：模型里出现过的类型都要认，认不出的一律当二进制流
        static std::string live2d_mime(std::string ext)
        {
            ext = lower_ascii(ext);
            if (!ext.empty() && ext.front() == '.')
                ext.erase(ext.begin());
            if (ext == "js" || ext == "mjs")
                return "text/javascript; charset=utf-8";
            if (ext == "json" || ext == "mtn")
                return "application/json; charset=utf-8";
            if (ext == "html")
                return "text/html; charset=utf-8";
            if (ext == "png")
                return "image/png";
            if (ext == "jpg" || ext == "jpeg")
                return "image/jpeg";
            if (ext == "webp")
                return "image/webp";
            if (ext == "wasm")
                return "application/wasm";
            if (ext == "wav")
                return "audio/wav";
            if (ext == "mp3")
                return "audio/mpeg";
            if (ext == "ogg")
                return "audio/ogg";
            return "application/octet-stream";
        }

        // ---- lifecycle -----------------------------------------------------
        std::string start(int port, int n_threads)
        {
            ros.on("", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string html = app::kUiHtml;
                bool from_disk = false;
                if (!ui_path.empty())
                {
                    std::ifstream f(ui_path, std::ios::binary);
                    if (f)
                    {
                        html.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                        from_disk = true;
                    }
                }
                out = http_response("200 OK", "text/html; charset=utf-8", html,
                                    from_disk ? "Cache-Control: no-store\r\n" : "");
                return rt::FLAG_DONE; });

            ros.on("favicon.ico", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                out = "HTTP/1.1 204 No Content\r\n\r\n";
                return rt::FLAG_DONE; });

            // GET /api/state
            ros.on("api/state", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                out = http_json(core.exec_command("state", nlohmann::json::object()));
                return rt::FLAG_DONE; });

            // GET /api/audio/<name>
            ros.on("api/audio/:name", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &params) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string name = params.count("name") ? params.at("name") : "";
                if (!plain_name_ok(name))
                {
                    out = text_response("400 Bad Request", "bad audio name");
                    return rt::FLAG_DONE;
                }
                std::error_code ec;
                auto base = std::filesystem::weakly_canonical(audio_dir(), ec);
                auto file = std::filesystem::weakly_canonical(audio_dir() / name, ec);
                if (!ec && file.parent_path() == base && std::filesystem::is_regular_file(file, ec))
                {
                    std::string body = read_file(file);
                    std::string ext = file.extension().string();
                    if (!ext.empty())
                        ext.erase(ext.begin());
                    std::string ctype = voice::mime_for(ext);
                    out = http_response("200 OK", ctype, body, "Cache-Control: private, max-age=3600\r\n");
                    return rt::FLAG_DONE;
                }
                out = text_response("404 Not Found", "audio not found");
                return rt::FLAG_DONE; });

            // GET /api/avatar/<id>
            ros.on("api/avatar/:id", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &params) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string id = params.count("id") ? params.at("id") : "";
                if (!persona_id_ok(id))
                {
                    out = text_response("400 Bad Request", "bad persona id");
                    return rt::FLAG_DONE;
                }
                std::error_code ec;
                auto dir = std::filesystem::weakly_canonical(persona_dir(id), ec);
                if (!ec && std::filesystem::is_directory(dir, ec))
                    for (auto &e : std::filesystem::directory_iterator(dir, ec))
                    {
                        if (ec || !e.is_regular_file(ec) || e.path().stem() != "avatar")
                            continue;
                        std::string body = read_file(e.path());
                        if (body.empty())
                            continue;
                        std::string ext = e.path().extension().string();
                        if (!ext.empty())
                            ext.erase(ext.begin());
                        std::string ctype = ext == "png" ? "image/png"
                                           : (ext == "gif" ? "image/gif"
                                                           : (ext == "webp" ? "image/webp" : "image/jpeg"));
                        out = http_response("200 OK", ctype, body, "Cache-Control: no-store\r\n");
                        return rt::FLAG_DONE;
                    }
                out = text_response("404 Not Found", "avatar not found");
                return rt::FLAG_DONE; });

            // GET /api/uploads/<pid>/<file> —— 聊天附件（桌面粘贴/拖入、IM 进来的媒体）
            ros.on("api/uploads/:pid/:file", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &params) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string pid = params.count("pid") ? params.at("pid") : "";
                std::string name = percent_decode(params.count("file") ? params.at("file") : "");
                if (!upload_pid_ok(pid) || !upload_name_ok(name))
                {
                    out = text_response("400 Bad Request", "bad upload path");
                    return rt::FLAG_DONE;
                }
                std::error_code ec;
                auto base = std::filesystem::weakly_canonical(media::uploads_dir(pid), ec);
                auto file = std::filesystem::weakly_canonical(media::uploads_dir(pid) / name, ec);
                if (!ec && file.parent_path() == base && std::filesystem::is_regular_file(file, ec))
                {
                    out = http_response("200 OK", media::mime_of(file.string()), read_file(file),
                                        "Cache-Control: private, max-age=3600\r\n");
                    return rt::FLAG_DONE;
                }
                out = text_response("404 Not Found", "upload not found");
                return rt::FLAG_DONE; });

            // GET /api/stickers/<pid>/<name> —— 表情包（只发图片；名字来自 sticker 目录）
            ros.on("api/stickers/:pid/:name", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &params) -> int
                   {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string pid = params.count("pid") ? params.at("pid") : "";
                std::string name = percent_decode(params.count("name") ? params.at("name") : "");
                if (!upload_pid_ok(pid) || name.empty() || name.size() > 64)
                {
                    out = text_response("400 Bad Request", "bad sticker name");
                    return rt::FLAG_DONE;
                }
                std::string path = media::sticker_path(pid, name);
                if (path.empty())
                {
                    out = text_response("404 Not Found", "sticker not found");
                    return rt::FLAG_DONE;
                }
                out = http_response("200 OK", media::mime_of(path), read_file(path),
                                    "Cache-Control: no-store\r\n");
                return rt::FLAG_DONE; });

            // GET /api/live2d/<path…> —— 舞台资源：运行时库 + 模型文件
            // 路由只按层数精确匹配，所以 1..5 层各注册一次，交给同一个处理函数。
            auto serve_live2d = [this](std::string &req, std::string &out,
                                       const std::map<std::string, std::string> &params) -> int
            {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string rel;
                for (const char *k : {"a", "b", "c", "d", "e"})
                {
                    auto it = params.find(k);
                    if (it == params.end())
                        break;
                    if (!rel.empty())
                        rel += '/';
                    rel += percent_decode(it->second);
                }
                // 逐段白名单：模型目录是用户自己放的，不能让它变成任意文件读取
                std::vector<std::string> segs;
                {
                    std::string cur;
                    for (char c : rel)
                    {
                        if (c == '/')
                        {
                            segs.push_back(cur);
                            cur.clear();
                        }
                        else
                            cur += c;
                    }
                    segs.push_back(cur);
                }
                for (auto &seg : segs)
                {
                    if (seg.empty() || seg == "." || seg == ".." || seg.size() > 96 ||
                        seg.find('\\') != std::string::npos || seg.find(':') != std::string::npos)
                    {
                        out = text_response("400 Bad Request", "bad live2d path");
                        return rt::FLAG_DONE;
                    }
                }
                std::error_code ec;
                auto base = std::filesystem::weakly_canonical(live2d_dir(), ec);
                auto file = std::filesystem::weakly_canonical(live2d_dir() / rel, ec);
                std::string parent = file.parent_path().string();
                std::string root_s = base.string();
                bool inside = parent == root_s ||
                              parent.rfind(root_s + std::string(1, (char)std::filesystem::path::preferred_separator), 0) == 0;
                if (!ec && inside && std::filesystem::is_regular_file(file, ec))
                {
                    out = http_response("200 OK", live2d_mime(file.extension().string()), read_file(file),
                                        "Cache-Control: no-store\r\n");
                    return rt::FLAG_DONE;
                }
                out = text_response("404 Not Found", "live2d file not found");
                return rt::FLAG_DONE;
            };
            for (const char *route : {"api/live2d/:a", "api/live2d/:a/:b", "api/live2d/:a/:b/:c",
                                      "api/live2d/:a/:b/:c/:d", "api/live2d/:a/:b/:c/:d/:e"})
                ros.on(route, serve_live2d);

            // POST /api/cmd/<name>
            ros.on("api/cmd/:name", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &params) -> int
                   {
                if (std::string bad = check_request(req, "POST"); !bad.empty())
                {
                    out = std::move(bad);
                    return rt::FLAG_DONE;
                }
                std::string method, path, body;
                parse_request(req, method, path, body);
                std::string cmd = params.count("name") ? params.at("name") : "";
                nlohmann::json args = nlohmann::json::object();
                if (!body.empty())
                {
                    auto j = nlohmann::json::parse(body, nullptr, false);
                    if (j.is_object())
                        args = std::move(j);
                }
                out = http_json(core.exec_command(cmd, args));
                return rt::FLAG_DONE; });

            // GET|POST /api/im/<id> —— IM 平台回调
            // 这是唯一一个**不做 loopback Host 校验**的路由：企业微信 / 公众号的
            // 回调必须经过反向代理（frp、ngrok、cpolar），Host 到了这里是公网域名。
            // 鉴权因此下沉到渠道自己：wecom 验签名（token + AES），onebot / bridge
            // 验 token；没配 token 的渠道 core 只接受本机直连。浏览器发起的跨站
            // 请求一律挡掉（Sec-Fetch-Site: cross-site）。
            ros.on("api/im/:id", [this](std::string &req, std::string &out, const std::map<std::string, std::string> &params) -> int
                   {
                if (lower_ascii(servic::header_value(req, "Sec-Fetch-Site")) == "cross-site")
                {
                    out = text_response("403 Forbidden", "cross-site request refused");
                    return rt::FLAG_DONE;
                }
                std::string method, path, body;
                parse_request(req, method, path, body);
                im::request rq;
                rq.method = method;
                rq.query = request_query(req);
                rq.auth = servic::header_value(req, "Authorization");
                rq.secret = servic::header_value(req, "X-Im-Secret");
                rq.body = std::move(body);
                rq.loopback = host_allowed(servic::header_value(req, "Host"));
                auto [code, payload] = core.im_webhook(params.count("id") ? params.at("id") : "", rq);
                std::string status = code == 200 ? "200 OK"
                                    : code == 400 ? "400 Bad Request"
                                    : code == 403 ? "403 Forbidden"
                                    : code == 404 ? "404 Not Found"
                                    : code == 405 ? "405 Method Not Allowed"
                                                  : "500 Internal Server Error";
                out = text_response(status, payload);
                return rt::FLAG_DONE; });

            // GET /api/events — SSE
            ros.on_stream("api/events", [this](std::string &req, rt::WriteCallback write, const std::map<std::string, std::string> &)
                          {
                if (std::string bad = check_request(req, "GET"); !bad.empty())
                {
                    write(bad);
                    return;
                }
                write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                      "Cache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
                write(": connected\n\n");
                auto conn = std::make_shared<sse_conn>();
                conn->write = [write](const std::string &s) { write(s); };
                int sink_id = core.bus.add([conn](const std::string &line)
                {
                    std::lock_guard<std::mutex> lk(conn->mx);
                    if (!conn->alive)
                        return;
                    if (conn->q.size() >= sse_conn::kMaxQueue)
                    {
                        conn->alive = false;
                        conn->cv.notify_all();
                        return;
                    }
                    conn->q.push_back(line);
                    conn->cv.notify_one();
                });
                {
                    std::lock_guard<std::mutex> lk(conns_mx);
                    conns.push_back(conn);
                }
                std::unique_lock<std::mutex> lk(conn->mx);
                auto beat = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (conn->alive)
                {
                    if (!conn->q.empty())
                    {
                        std::deque<std::string> batch;
                        batch.swap(conn->q);
                        lk.unlock();
                        bool failed = false;
                        for (auto &line : batch)
                        {
                            try
                            {
                                conn->write("data: " + line + "\n\n");
                            }
                            catch (...)
                            {
                                failed = true;
                                break;
                            }
                        }
                        lk.lock();
                        if (failed)
                        {
                            conn->alive = false;
                            break;
                        }
                        continue;
                    }
                    if (conn->cv.wait_until(lk, beat, [&]
                                            { return !conn->alive || !conn->q.empty(); }))
                    {
                        if (!conn->alive)
                            break;
                        continue;
                    }
                    lk.unlock();
                    bool failed = false;
                    try
                    {
                        conn->write(": ping\n\n");
                    }
                    catch (...)
                    {
                        failed = true;
                    }
                    lk.lock();
                    if (failed)
                    {
                        conn->alive = false;
                        break;
                    }
                    beat = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                }
                lk.unlock();
                core.bus.remove(sink_id); });

            try
            {
                // 上限放宽到 128 MiB：头像、浏览器录音、聊天附件（48 MiB 原始文件
                // 走 base64 后约 64 MiB）都走 JSON body
                server = std::make_unique<servic::Server>(io, "127.0.0.1", (unsigned short)port, 128 * 1024 * 1024);
            }
            catch (const std::exception &e)
            {
                server.reset();
                bound_port = 0;
                return std::format("cannot listen on 127.0.0.1:{}: {}", port, e.what());
            }
            bound_port = port;
            if (n_threads < 1)
                n_threads = 1;
            work.emplace(asio::make_work_guard(io));
            threads.emplace_back([this]
                                 { server->run(ros); });
            for (int i = 1; i < n_threads; i++)
                threads.emplace_back([this]
                                     { io.run(); });
            return "";
        }

        void stop()
        {
            std::vector<std::shared_ptr<sse_conn>> live;
            {
                std::lock_guard<std::mutex> lk(conns_mx);
                for (auto &w : conns)
                    if (auto s = w.lock())
                        live.push_back(std::move(s));
                conns.clear();
            }
            for (auto &c : live)
            {
                std::lock_guard<std::mutex> lk(c->mx);
                c->alive = false;
            }
            for (auto &c : live)
                c->cv.notify_all();
            if (server)
                server->stop_handlers(15000);
            work.reset();
            io.stop();
            for (auto &t : threads)
                if (t.joinable())
                    t.join();
            threads.clear();
            server.reset();
            bound_port = 0;
        }
    };
} // namespace app
