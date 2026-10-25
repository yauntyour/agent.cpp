#include "server/http_server.hpp"

#include "core/log.hpp"

namespace http
{
    // ==================================================================
    //  平台层：socket 基础操作
    // ==================================================================
    namespace
    {
#ifdef _WIN32
        using sock_t = SOCKET;
        constexpr sock_t kInvalid = INVALID_SOCKET;
        void close_sock(sock_t s) { closesocket(s); }
        int last_error() { return WSAGetLastError(); }
        bool would_block(int e) { return e == WSAEWOULDBLOCK; }
        void init_sockets()
        {
            static std::once_flag once;
            std::call_once(once, []()
                           {
                               WSADATA wsa;
                               WSAStartup(MAKEWORD(2, 2), &wsa);
                           });
        }
        void shutdown_sock(sock_t s) { ::shutdown(s, SD_BOTH); }
#else
        using sock_t = int;
        constexpr sock_t kInvalid = -1;
        void close_sock(sock_t s) { ::close(s); }
        int last_error() { return errno; }
        bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
        void init_sockets() {}
        void shutdown_sock(sock_t s) { ::shutdown(s, SHUT_RDWR); }
#endif

        sock_t to_sock(std::intptr_t v) { return static_cast<sock_t>(v); }

        void set_reuse(sock_t s)
        {
            // Windows 上 SO_REUSEADDR 允许抢占别人的端口，但关掉它又会让
            // 「杀掉进程立刻重启」因为 TIME_WAIT 而 bind 失败。
            // 这里保留 SO_REUSEADDR，改用 connect 探测判断端口是否已被占用。
            int yes = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&yes), sizeof(yes));
        }

        // 端口上是否已经有人在 listen（连得上就算被占用）
        bool port_in_use(const std::string &host, unsigned short port)
        {
            if (port == 0)
                return false;

            sock_t s = ::socket(AF_INET, SOCK_STREAM, 0);
            if (s == kInvalid)
                return false;

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            const std::string target = (host.empty() || host == "0.0.0.0") ? "127.0.0.1" : host;
            if (inet_pton(AF_INET, target.c_str(), &addr.sin_addr) != 1)
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            const bool up = ::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
            close_sock(s);
            return up;
        }

        void set_timeouts(sock_t s, int recv_sec, int send_sec)
        {
#ifdef _WIN32
            DWORD tv = static_cast<DWORD>(recv_sec * 1000);
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
            tv = static_cast<DWORD>(send_sec * 1000);
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&tv), sizeof(tv));
#else
            timeval tv{};
            tv.tv_sec = recv_sec;
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            tv.tv_sec = send_sec;
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
        }

        std::string lower(std::string s)
        {
            for (auto &c : s)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }

        std::string trim(std::string s)
        {
            size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos)
                return {};
            size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        }
    } // namespace

    // ==================================================================
    //  Request
    // ==================================================================
    std::string Request::header(const std::string &name) const
    {
        auto it = headers.find(lower(name));
        return it == headers.end() ? std::string() : it->second;
    }

    json Request::json_body() const
    {
        if (body.empty())
            return json::object();
        return json::parse(body);
    }

    std::string Request::query_param(const std::string &name) const
    {
        size_t pos = 0;
        while (pos < query.size())
        {
            size_t amp = query.find('&', pos);
            std::string pair = query.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
            size_t eq = pair.find('=');
            if (eq != std::string::npos && pair.substr(0, eq) == name)
                return pair.substr(eq + 1);
            if (amp == std::string::npos)
                break;
            pos = amp + 1;
        }
        return {};
    }

    // ==================================================================
    //  Router
    // ==================================================================
    std::string Router::normalize(const std::string &path)
    {
        std::string p = path;
        // 去掉 query
        size_t q = p.find('?');
        if (q != std::string::npos)
            p = p.substr(0, q);
        // 折叠重复的 '/'
        std::string out;
        out.reserve(p.size());
        for (char c : p)
        {
            if (c == '/' && !out.empty() && out.back() == '/')
                continue;
            out += c;
        }
        if (out.size() > 1 && out.back() == '/')
            out.pop_back();
        if (out.empty())
            out = "/";
        return out;
    }

    std::vector<std::string> Router::split_path(const std::string &path)
    {
        std::vector<std::string> segs;
        std::string cur;
        for (char c : path)
        {
            if (c == '/')
            {
                if (!cur.empty())
                    segs.push_back(cur);
                cur.clear();
            }
            else
                cur += c;
        }
        if (!cur.empty())
            segs.push_back(cur);
        return segs;
    }

    void Router::on(const std::string &path, Handler h)
    {
        on("", path, std::move(h));
    }

    void Router::on(const std::string &method, const std::string &path, Handler h)
    {
        Route r;
        r.method = lower(method);
        r.pattern = normalize(path);
        r.segs = split_path(r.pattern);
        r.handler = std::move(h);
        route_list_.push_back(r.method.empty() ? r.pattern : (r.method + " " + r.pattern));
        routes_.push_back(std::move(r));
    }

    void Router::on_stream(const std::string &path, StreamHandler h)
    {
        streams_[normalize(path)] = std::move(h);
        route_list_.push_back("STREAM " + normalize(path));
    }

    void Router::on_prefix(const std::string &prefix, Handler h)
    {
        prefixes_.emplace_back(normalize(prefix), std::move(h));
        route_list_.push_back("PREFIX " + normalize(prefix));
    }

    bool Router::match_prefix(const std::string &path, Handler &out) const
    {
        const std::string p = normalize(path);
        for (const auto &kv : prefixes_)
        {
            if (p.compare(0, kv.first.size(), kv.first) == 0)
            {
                out = kv.second;
                return true;
            }
        }
        return false;
    }

    bool Router::match(const std::string &method, const std::string &path,
                       Handler &out, std::map<std::string, std::string> &params) const
    {
        std::string m = lower(method);
        auto segs = split_path(normalize(path));
        for (const auto &r : routes_)
        {
            if (!r.method.empty() && r.method != m)
                continue;
            if (r.segs.size() != segs.size())
                continue;
            std::map<std::string, std::string> tmp;
            bool ok = true;
            for (size_t i = 0; i < segs.size(); ++i)
            {
                const std::string &pat = r.segs[i];
                if (!pat.empty() && pat[0] == ':')
                    tmp[pat.substr(1)] = segs[i];
                else if (pat != segs[i])
                {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                continue;
            params = std::move(tmp);
            out = r.handler;
            return true;
        }
        return false;
    }

    bool Router::match_stream(const std::string &path, StreamHandler &out) const
    {
        auto it = streams_.find(normalize(path));
        if (it == streams_.end())
            return false;
        out = it->second;
        return true;
    }

    // ==================================================================
    //  响应构造
    // ==================================================================
    std::string status_text(int status)
    {
        switch (status)
        {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "OK";
        }
    }

    std::string build_response(int status, const std::string &content_type,
                               const std::string &body,
                               const std::vector<std::pair<std::string, std::string>> &extra,
                               bool cors)
    {
        std::string ct = content_type;
        // 文本类响应统一补 charset，避免中文乱码
        if ((ct.find("text/") == 0 || ct.find("application/json") == 0 ||
             ct.find("application/javascript") == 0 || ct.find("image/svg") == 0) &&
            ct.find("charset=") == std::string::npos)
            ct += "; charset=utf-8";

        std::ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << status_text(status) << "\r\n";
        oss << "Content-Type: " << ct << "\r\n";
        oss << "Content-Length: " << body.size() << "\r\n";
        oss << "Connection: close\r\n";
        oss << "Cache-Control: no-store\r\n";
        if (cors)
        {
            oss << "Access-Control-Allow-Origin: *\r\n";
            oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
            oss << "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
        }
        for (const auto &kv : extra)
            oss << kv.first << ": " << kv.second << "\r\n";
        oss << "\r\n";
        oss << body;
        return oss.str();
    }

    std::string extract_url(const std::string &raw)
    {
        size_t line_end = raw.find_first_of("\r\n");
        std::string first = raw.substr(0, line_end);
        std::istringstream iss(first);
        std::string method, url, version;
        if (iss >> method >> url >> version)
            return url;
        return {};
    }

    // ==================================================================
    //  Server
    // ==================================================================
    Server::Server(std::string host, unsigned short port, size_t max_body, size_t max_connections)
        : host_(std::move(host)), port_(port), max_body_(max_body), max_connections_(max_connections)
    {
        init_sockets();
    }

    Server::~Server() { stop(); }

    bool Server::start()
    {
        if (running_.load())
            return true;

        auto bind_listen = [this](unsigned short p) -> sock_t
        {
            sock_t ls = ::socket(AF_INET, SOCK_STREAM, 0);
            if (ls == kInvalid)
            {
                logx::error("http", "socket() failed: " + std::to_string(last_error()));
                return kInvalid;
            }
            set_reuse(ls);

            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(p);
            if (host_.empty() || host_ == "0.0.0.0")
                addr.sin_addr.s_addr = htonl(INADDR_ANY);
            else if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1)
                addr.sin_addr.s_addr = htonl(INADDR_ANY);

            if (::bind(ls, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
            {
                logx::error("http", "bind " + host_ + ":" + std::to_string(p) +
                                        " failed: " + std::to_string(last_error()));
                close_sock(ls);
                return kInvalid;
            }
            if (::listen(ls, 128) != 0)
            {
                logx::error("http", "listen failed: " + std::to_string(last_error()));
                close_sock(ls);
                return kInvalid;
            }
            return ls;
        };

        // 端口被别的进程占着时（Windows 上 bind 不一定报错），直接换临时端口
        unsigned short want = port_;
        if (want != 0 && ephemeral_fallback_ && port_in_use(host_, want))
        {
            logx::warn("http", "port " + std::to_string(want) + " is already in use, falling back to an ephemeral port");
            want = 0;
        }

        sock_t ls = bind_listen(want);
        if (ls == kInvalid && ephemeral_fallback_ && want != 0)
        {
            logx::warn("http", "port " + std::to_string(want) + " unavailable, falling back to an ephemeral port");
            ls = bind_listen(0);
        }
        if (ls == kInvalid)
            return false;

        // 端口 0 / 回退临时端口时由系统分配，回读真实端口
        {
            sockaddr_in actual{};
#ifdef _WIN32
            int len = sizeof(actual);
#else
            socklen_t len = sizeof(actual);
#endif
            if (getsockname(ls, reinterpret_cast<sockaddr *>(&actual), &len) == 0)
                port_ = ntohs(actual.sin_port);
        }

        listen_sock_ = static_cast<std::intptr_t>(ls);
        running_.store(true);
        accept_thread_ = std::thread([this]() { accept_loop(); });
        logx::info("http", "listening on " + host_ + ":" + std::to_string(port_));
        return true;
    }

    void Server::run()
    {
        if (!start())
            throw std::runtime_error("cannot listen on " + host_ + ":" + std::to_string(port_));
        if (accept_thread_.joinable())
            accept_thread_.join();
    }

    void Server::stop()
    {
        if (!running_.exchange(false))
            return;
        if (listen_sock_ != -1)
        {
            shutdown_sock(to_sock(listen_sock_));
            close_sock(to_sock(listen_sock_));
            listen_sock_ = -1;
        }
        if (accept_thread_.joinable())
            accept_thread_.join();
    }

    void Server::accept_loop()
    {
        while (running_.load())
        {
            sock_t ls = to_sock(listen_sock_);
            sockaddr_in peer{};
#ifdef _WIN32
            int len = sizeof(peer);
#else
            socklen_t len = sizeof(peer);
#endif
            sock_t client = ::accept(ls, reinterpret_cast<sockaddr *>(&peer), &len);
            if (client == kInvalid)
            {
                if (!running_.load())
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            if (active_.load() >= max_connections_)
            {
                const char *busy = "HTTP/1.1 503 Service Unavailable\r\n"
                                   "Content-Length: 0\r\nConnection: close\r\n\r\n";
                ::send(client, busy, static_cast<int>(std::strlen(busy)), 0);
                close_sock(client);
                continue;
            }

            active_.fetch_add(1);
            std::thread([this, client]()
                        {
                            try
                            {
                                handle_connection(static_cast<std::intptr_t>(client));
                            }
                            catch (const std::exception &e)
                            {
                                logx::error("http", std::string("connection error: ") + e.what());
                                close_sock(client);
                            }
                            catch (...)
                            {
                                close_sock(client);
                            }
                            active_.fetch_sub(1);
                        })
                .detach();
        }
    }

    bool Server::read_request(std::intptr_t isock, Request &req)
    {
        sock_t s = to_sock(isock);
        set_timeouts(s, 60, 60);

        std::string buf;
        buf.reserve(8192);
        char chunk[8192];
        size_t header_end = std::string::npos;

        while (header_end == std::string::npos)
        {
            int n = ::recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0)
                return false;
            buf.append(chunk, static_cast<size_t>(n));
            header_end = buf.find("\r\n\r\n");
            if (buf.size() > 1u * 1024 * 1024 && header_end == std::string::npos)
                return false; // 请求头过大
        }

        std::string head = buf.substr(0, header_end);
        req.raw = buf;
        req.body = buf.substr(header_end + 4);

        // 解析请求行
        size_t line_end = head.find("\r\n");
        std::string request_line = head.substr(0, line_end);
        {
            std::istringstream iss(request_line);
            std::string target;
            iss >> req.method >> target >> req.version;
            if (req.method.empty())
                return false;
            size_t q = target.find('?');
            req.path = q == std::string::npos ? target : target.substr(0, q);
            req.query = q == std::string::npos ? "" : target.substr(q + 1);
        }

        // 解析请求头
        size_t pos = (line_end == std::string::npos) ? head.size() : line_end + 2;
        while (pos < head.size())
        {
            size_t eol = head.find("\r\n", pos);
            std::string line = head.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            size_t colon = line.find(':');
            if (colon != std::string::npos)
                req.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
            if (eol == std::string::npos)
                break;
            pos = eol + 2;
        }

        // 读取请求体
        size_t need = 0;
        auto it = req.headers.find("content-length");
        if (it != req.headers.end())
        {
            try
            {
                need = static_cast<size_t>(std::stoull(it->second));
            }
            catch (...)
            {
                need = 0;
            }
        }
        if (need > max_body_)
            return false;

        while (req.body.size() < need)
        {
            int n = ::recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0)
                break;
            req.body.append(chunk, static_cast<size_t>(n));
        }
        return true;
    }

    void Server::write_all(std::intptr_t isock, const std::string &data)
    {
        sock_t s = to_sock(isock);
        size_t sent = 0;
        while (sent < data.size())
        {
            int n = ::send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0);
            if (n <= 0)
            {
                int e = last_error();
                if (would_block(e))
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                return;
            }
            sent += static_cast<size_t>(n);
        }
    }

    void Server::handle_connection(std::intptr_t isock)
    {
        sock_t s = to_sock(isock);
        Request req;
        if (!router_ || !read_request(isock, req))
        {
            close_sock(s);
            return;
        }

        // CORS 预检：统一放行
        if (req.method == "OPTIONS")
        {
            write_all(isock, build_response(204, "text/plain", ""));
            close_sock(s);
            return;
        }

        // 流式路由优先
        StreamHandler sh;
        if (router_->match_stream(req.path, sh))
        {
            auto write = [this, isock](const std::string &data) { write_all(isock, data); };
            try
            {
                sh(req.raw, write, req.params);
            }
            catch (const std::exception &e)
            {
                logx::error("http", std::string("stream handler: ") + e.what());
            }
            close_sock(s);
            return;
        }

        std::string output;
        Handler h;
        bool matched = router_->match(req.method, req.path, h, req.params);
        if (!matched)
            matched = router_->match_prefix(req.path, h);
        if (!matched)
            h = router_->fallback_handler();

        if (h)
        {
            try
            {
                int rc = h(req.raw, output, req.params);
                if (output.empty())
                    output = build_response(rc == FLAG_ERROR ? 500 : 404, "text/plain",
                                            rc == FLAG_ERROR ? "internal error" : "not found");
            }
            catch (const std::exception &e)
            {
                logx::error("http", std::string(req.method) + " " + req.path + ": " + e.what());
                output = build_response(500, "application/json",
                                        json{{"error", e.what()}}.dump());
            }
            catch (...)
            {
                output = build_response(500, "application/json", R"({"error":"unknown"})");
            }
        }
        else
        {
            output = build_response(404, "text/plain", "not found");
        }

        write_all(isock, output);
        close_sock(s);
    }
} // namespace http
