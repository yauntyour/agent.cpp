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
//
// 安全模型：服务是一个「能记事、能说话」的本机控制面，绝不能被网页或局域网
// 碰到。只绑 127.0.0.1；每个请求必须带 loopback Host、同源 Origin（或没有
// Origin）；跨站标记直接拒绝；没有 CORS。
#pragma once

#include "servic.hpp"

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
        static std::string read_file(const std::filesystem::path &p)
        {
            std::ifstream f(p, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
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
                // 上限放宽到 72 MiB：头像与浏览器侧录音都走 base64
                server = std::make_unique<servic::Server>(io, "127.0.0.1", (unsigned short)port, 72 * 1024 * 1024);
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
