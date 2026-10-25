// ============================================================================
//  server/http_server.hpp —— 自带的 HTTP/1.1 服务端（无 Boost、无第三方依赖）
//
//  为什么不用现成的库：本项目目标是“一个可执行文件 + 一个 .agent 目录”，
//  自带一个两百来行的阻塞式 HTTP 服务端，可以去掉 Boost.Asio 依赖，
//  同时拿到路由兜底（静态资源目录）这种原框架没有的能力。
//
//  线程模型：
//    - 1 个 accept 线程
//    - 每个连接 1 个工作线程（并发上限 max_connections）
//    - SSE 长连接会独占一个线程，这是刻意的：聊天请求本身已被
//      agent::chat_mutex 串行化，连接数天然很低。
//
//  路由：
//    - 精确路径      /api/session/list
//    - 动态参数      /api/todos/:id       → params["id"]
//    - 流式处理器    on_stream()           → 自行写 SSE 分块
//    - 兜底处理器    fallback()           → WebUI 静态资源用
// ============================================================================
#pragma once
#ifndef AGENTCPP_SERVER_HTTP_SERVER_HPP
#define AGENTCPP_SERVER_HTTP_SERVER_HPP

#include "core/common.hpp"

namespace http
{
    struct Request
    {
        std::string method;
        std::string path;    // 不含 query
        std::string query;   // 原始 query 串
        std::string version;
        std::string raw;     // 完整原始请求（兼容旧处理函数）
        std::string body;
        std::map<std::string, std::string> headers; // 键为小写
        std::map<std::string, std::string> params;  // 路由参数

        std::string header(const std::string &name) const;
        json json_body() const;
        std::string query_param(const std::string &name) const;
    };

    // 处理函数返回 int：0 表示已写出响应，非 0 表示出错
    using Handler = std::function<int(std::string &input, std::string &output,
                                      const std::map<std::string, std::string> &params)>;
    using WriteCallback = std::function<void(const std::string &)>;
    using StreamHandler = std::function<void(std::string &input, WriteCallback write,
                                             const std::map<std::string, std::string> &params)>;

    constexpr int FLAG_DONE = 0;
    constexpr int FLAG_ERROR = 1;
    constexpr int FLAG_WARN = 2;

    // ------------------------------------------------------------------
    //  路由表
    // ------------------------------------------------------------------
    class Router
    {
    public:
        void on(const std::string &path, Handler h);
        void on(const std::string &method, const std::string &path, Handler h);
        void on_stream(const std::string &path, StreamHandler h);
        // 前缀路由：任何以 prefix 开头的路径都会命中（用于 Webhook 这类可配置路径）
        void on_prefix(const std::string &prefix, Handler h);
        void fallback(Handler h) { fallback_ = std::move(h); }

        // 命中则填充 params 并返回 true
        bool match(const std::string &method, const std::string &path,
                   Handler &out, std::map<std::string, std::string> &params) const;
        bool match_stream(const std::string &path, StreamHandler &out) const;
        bool match_prefix(const std::string &path, Handler &out) const;

        Handler &fallback_handler() { return fallback_; }
        const std::vector<std::string> &routes() const { return route_list_; }

        static std::string normalize(const std::string &path);
        static std::vector<std::string> split_path(const std::string &path);

    private:
        struct Route
        {
            std::string method; // 空 = 任意方法
            std::vector<std::string> segs;
            Handler handler;
            std::string pattern;
        };
        std::vector<Route> routes_;
        std::vector<std::string> route_list_;
        std::vector<std::pair<std::string, Handler>> prefixes_;
        std::map<std::string, StreamHandler> streams_;
        Handler fallback_;
    };

    // ------------------------------------------------------------------
    //  服务器
    // ------------------------------------------------------------------
    class Server
    {
    public:
        Server(std::string host, unsigned short port, size_t max_body = 32u * 1024 * 1024,
               size_t max_connections = 64);
        ~Server();

        void attach(Router *router) { router_ = router; }

        // 端口被占用时是否回退到系统临时端口（桌面模式下服务端只是内部通道，
        // 端口冲突不应该让整个程序起不来）
        void allow_ephemeral_fallback(bool v) { ephemeral_fallback_ = v; }

        // 绑定并开始 accept（非阻塞）
        bool start();
        // 阻塞运行（内部调用 start）
        void run();
        void stop();

        // 实际监听端口（构造时传 0 或回退临时端口后才是真值）
        unsigned short port() const { return port_; }

    private:
        void accept_loop();
        void handle_connection(std::intptr_t sock);
        bool read_request(std::intptr_t sock, Request &req);
        void write_all(std::intptr_t sock, const std::string &data);

        std::string host_;
        unsigned short port_;
        size_t max_body_;
        size_t max_connections_;
        Router *router_ = nullptr;
        bool ephemeral_fallback_ = false;
        std::intptr_t listen_sock_ = -1;
        std::atomic<bool> running_{false};
        std::atomic<size_t> active_{0};
        std::thread accept_thread_;
    };

    // ------------------------------------------------------------------
    //  响应构造
    // ------------------------------------------------------------------
    std::string build_response(int status, const std::string &content_type,
                               const std::string &body,
                               const std::vector<std::pair<std::string, std::string>> &extra = {},
                               bool cors = true);

    std::string status_text(int status);

    // 从原始请求文本里取 URL（兼容旧代码）
    std::string extract_url(const std::string &raw);
} // namespace http

#endif // AGENTCPP_SERVER_HTTP_SERVER_HPP
