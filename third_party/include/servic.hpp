#pragma once
#ifndef __SERVER__H__
#define __SERVER__H__
// servic.hpp — a minimal HTTP/1.1 server on Boost.Asio for the cell-app web
// service.
//
// Patched for cell-app (see README, "Vendored servic.hpp"):
//   1. query strings are stripped from the routed path, so the page's
//      ?theme= / ?dock= / ?mock=1 parameters never reach the router;
//   2. a failed socket write throws, so a stream (SSE) handler can notice a
//      disconnected client;
//   3. every request runs on a dedicated worker thread, not on an
//      io_context thread: a long command or a long-lived SSE stream can no
//      longer starve the accept/read side (the io pool stays free). The
//      worker threads are bounded (kMaxHandlerThreads) and tracked, so
//      http_service::stop() can wait for them;
//   4. the listen endpoint is explicit: the shell binds 127.0.0.1 only;
//   5. the request head is bounded (64 KiB), Content-Length is parsed
//      case-insensitively, invalid lengths are rejected and chunked request
//      bodies get 411 instead of being silently mis-parsed; Expect:
//      100-continue is honoured so large uploads by curl do not stall.
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "router/router.hpp"

namespace servic
{
    namespace asio = boost::asio;

    std::string extract_url(const std::string &raw_data)
    {
        if (raw_data.empty())
            return "";

        // 1. 找到第一行的结束位置 (\r 或 \n)
        size_t line_end = raw_data.find_first_of("\r\n");
        std::string first_line;

        if (line_end == std::string::npos)
        {
            // 如果没有换行符，假设整行就是请求行（兼容某些简陋客户端）
            first_line = raw_data;
        }
        else
        {
            first_line = raw_data.substr(0, line_end);
        }

        // 2. 使用 stringstream 按空格分割
        // 格式: METHOD URL VERSION
        std::istringstream iss(first_line);
        std::string method, url, version;

        if (iss >> method >> url >> version)
        {
            // 路由只认路径：查询串（?theme=… / ?dock=… / ?mock=1）由页面自己
            // 读取，留在这里会让 "/" 匹配不上而回 404
            size_t q = url.find('?');
            if (q != std::string::npos)
                url = url.substr(0, q);
            return url;
        }

        // 解析失败（格式不对）
        return "";
    }

    // 请求方法（第一行的第一个 token）
    inline std::string request_method(const std::string &raw)
    {
        std::string first = raw.substr(0, raw.find_first_of("\r\n"));
        std::istringstream iss(first);
        std::string method;
        iss >> method;
        return method;
    }

    // 大小写不敏感的头字段查找；只扫描请求头（第一处 \r\n\r\n 之前）
    inline std::string header_value(const std::string &raw, std::string_view name)
    {
        size_t head_end = raw.find("\r\n\r\n");
        std::string_view block(raw.data(), head_end == std::string::npos ? raw.size() : head_end);
        size_t first = block.find("\r\n");
        if (first == std::string_view::npos)
            return "";
        size_t pos = first + 2;
        while (pos < block.size())
        {
            size_t eol = block.find("\r\n", pos);
            if (eol == std::string_view::npos)
                eol = block.size();
            std::string_view line = block.substr(pos, eol - pos);
            size_t colon = line.find(':');
            if (colon != std::string_view::npos)
            {
                std::string_view key = line.substr(0, colon);
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
                    key.remove_suffix(1);
                if (key.size() == name.size())
                {
                    bool same = true;
                    for (size_t i = 0; i < key.size(); ++i)
                        if (std::tolower((unsigned char)key[i]) != std::tolower((unsigned char)name[i]))
                        {
                            same = false;
                            break;
                        }
                    if (same)
                    {
                        std::string_view val = line.substr(colon + 1);
                        while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
                            val.remove_prefix(1);
                        while (!val.empty() && (val.back() == ' ' || val.back() == '\t'))
                            val.remove_suffix(1);
                        return std::string(val);
                    }
                }
            }
            if (eol == block.size())
                break;
            pos = eol + 2;
        }
        return "";
    }

    // Content-Length：空 = 0；非法（非数字/超长）= ok=false
    inline long long content_length_of(const std::string &raw, bool &ok)
    {
        ok = true;
        std::string v = header_value(raw, "Content-Length");
        if (v.empty())
            return 0;
        if (v.size() > 20)
        {
            ok = false;
            return 0;
        }
        long long n = 0;
        for (char c : v)
        {
            if (c < '0' || c > '9')
            {
                ok = false;
                return 0;
            }
            n = n * 10 + (c - '0');
        }
        return n;
    }

    // =====================================================================
    //  handler_registry — bounds and tracks the per-request worker threads.
    // =====================================================================
    struct handler_registry
    {
        std::mutex mx;
        std::condition_variable cv;
        int active = 0;
        bool stopping = false;

        bool enter(size_t cap)
        {
            std::lock_guard<std::mutex> lk(mx);
            if (stopping || active >= (int)cap)
                return false;
            ++active;
            return true;
        }
        void leave()
        {
            {
                std::lock_guard<std::mutex> lk(mx);
                if (active > 0)
                    --active;
            }
            cv.notify_all();
        }
        void stop()
        {
            {
                std::lock_guard<std::mutex> lk(mx);
                stopping = true;
            }
            cv.notify_all();
        }
        bool stopping_now()
        {
            std::lock_guard<std::mutex> lk(mx);
            return stopping;
        }
        bool wait_idle(int timeout_ms)
        {
            std::unique_lock<std::mutex> lk(mx);
            cv.wait_for(lk, std::chrono::milliseconds(timeout_ms == 0 ? 1 : timeout_ms), [&]
                        { return active == 0; });
            return active == 0;
        }
    };

    // =====================================================================
    //  Session — reads one request, then hands it to a worker thread.
    // =====================================================================
    class Session : public std::enable_shared_from_this<Session>
    {
    public:
        static constexpr size_t kMaxHeaderBytes = 64 * 1024;
        static constexpr size_t kMaxHandlerThreads = 64;

        Session(asio::ip::tcp::socket socket, size_t max_buf, std::shared_ptr<handler_registry> registry)
            : socket_(std::move(socket)), max_buf_(max_buf), registry_(std::move(registry)) {}

        asio::awaitable<void> start(rt::router &ros)
        {
            co_spawn(socket_.get_executor(),
                     [self = shared_from_this(), &ros]() -> asio::awaitable<void>
                     {
                         try
                         {
                             // 1) request head (bounded)
                             asio::streambuf head(kMaxHeaderBytes);
                             co_await asio::async_read_until(self->socket_, head, "\r\n\r\n", asio::use_awaitable);
                             std::string request(asio::buffers_begin(head.data()), asio::buffers_end(head.data()));
                             size_t head_size = request.find("\r\n\r\n");
                             if (head_size == std::string::npos)
                             {
                                 co_await self->respond_and_close("HTTP/1.1 400 Bad Request", "malformed request");
                                 co_return;
                             }
                             head_size += 4;

                             // 2) body
                             bool len_ok = true;
                             long long content_length = content_length_of(request, len_ok);
                             if (!len_ok)
                             {
                                 co_await self->respond_and_close("HTTP/1.1 400 Bad Request", "invalid content-length");
                                 co_return;
                             }
                             if (!header_value(request, "Transfer-Encoding").empty())
                             {
                                 co_await self->respond_and_close("HTTP/1.1 411 Length Required",
                                                                  "chunked request bodies are not supported");
                                 co_return;
                             }
                             if (content_length < 0 || (size_t)content_length > self->max_buf_)
                             {
                                 co_await self->respond_and_close("HTTP/1.1 413 Payload Too Large", "request body too large");
                                 co_return;
                             }
                             if (content_length > 0 && request.size() < head_size + (size_t)content_length)
                             {
                                 std::string expect = header_value(request, "Expect");
                                 for (char &c : expect)
                                     c = (char)std::tolower((unsigned char)c);
                                 if (expect == "100-continue")
                                 {
                                     std::string cont = "HTTP/1.1 100 Continue\r\n\r\n";
                                     co_await asio::async_write(self->socket_, asio::buffer(cont), asio::use_awaitable);
                                 }
                                 size_t want = (size_t)content_length - (request.size() - head_size);
                                 asio::streambuf body(self->max_buf_);
                                 co_await asio::async_read(self->socket_, body, asio::transfer_exactly(want), asio::use_awaitable);
                                 request.append(asio::buffers_begin(body.data()), asio::buffers_end(body.data()));
                             }

                             // 3) route
                             std::string url = extract_url(request);
                             auto [node_ptr, params] = ros.get(url);
                             rt::StreamHandlerFunc stream_handler;
                             rt::HandlerFunc normal_handler;
                             if (ros.has_stream_handler(url))
                             {
                                 stream_handler = ros.get_stream_handler(url);
                                 if (!stream_handler)
                                 {
                                     co_await self->respond_and_close("HTTP/1.1 404 Not Found", "");
                                     co_return;
                                 }
                             }
                             else
                             {
                                 auto node = node_ptr.lock();
                                 if (!node)
                                 {
                                     co_await self->respond_and_close("HTTP/1.1 404 Not Found", "");
                                     co_return;
                                 }
                                 normal_handler = node->func;
                             }

                             // 4) hand the request to a dedicated worker thread; the io
                             //    thread goes straight back to accepting/reading.
                             if (!self->registry_->enter(kMaxHandlerThreads))
                             {
                                 co_await self->respond_and_close("HTTP/1.1 503 Service Unavailable", "server busy");
                                 co_return;
                             }
                             std::thread([self, request = std::move(request), params = std::move(params),
                                          stream_handler = std::move(stream_handler), normal_handler = std::move(normal_handler)]() mutable
                                         {
                                 try
                                 {
                                     if (stream_handler)
                                     {
                                         auto write_cb = [self](const std::string &chunk)
                                         { asio::write(self->socket_, asio::buffer(chunk)); };
                                         stream_handler(request, write_cb, params);
                                     }
                                     else
                                     {
                                         std::string response;
                                         int rc = normal_handler(request, response, params);
                                         if (rc == rt::FLAG_ERROR && response.empty())
                                             response = "HTTP/1.1 500 Internal Server Error\r\n"
                                                        "Content-Length: 0\r\nConnection: close\r\n\r\n";
                                         if (!response.empty())
                                             asio::write(self->socket_, asio::buffer(response));
                                     }
                                 }
                                 catch (const std::exception &e)
                                 {
                                     std::cerr << "request handler: " << e.what() << '\n';
                                 }
                                 catch (...)
                                 {
                                     std::cerr << "request handler: unknown error\n";
                                 }
                                 boost::system::error_code ec;
                                 self->socket_.close(ec);
                                 self->registry_->leave(); })
                                 .detach();
                             co_return;
                         }
                         catch (const std::exception &e)
                         {
                             std::cerr << "Disconnected with error:" << e.what() << '\n';
                         }
                         co_return;
                     },
                     asio::detached);
            co_return;
        }

    private:
        asio::awaitable<void> respond_and_close(const std::string &status, const std::string &body)
        {
            try
            {
                std::string r = status + "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
                                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                co_await asio::async_write(socket_, asio::buffer(r), asio::use_awaitable);
            }
            catch (...)
            {
            }
            boost::system::error_code ec;
            socket_.close(ec);
            co_return;
        }

        asio::ip::tcp::socket socket_;
        size_t max_buf_ = 0;
        std::shared_ptr<handler_registry> registry_;
    };

    // =====================================================================
    //  Server — acceptor + io_context runner.
    // =====================================================================
    class Server
    {
    public:
        Server(asio::io_context &io_context, const std::string &address, unsigned short port, size_t max_buf = 300000)
            : handle(io_context), acceptor(io_context), address_(address), port_(port), max_buf_(max_buf)
        {
            boost::system::error_code ec;
            auto addr = asio::ip::make_address(address, ec);
            if (ec)
                throw std::runtime_error("invalid listen address: " + address);
            asio::ip::tcp::endpoint endpoint(addr, port);
            acceptor.open(endpoint.protocol(), ec);
            if (ec)
                throw boost::system::system_error(ec);
#if !defined(_WIN32)
            // SO_REUSEADDR is useful for a quick restart after TIME_WAIT on
            // POSIX. On Windows it means something else: it lets a second
            // process bind the SAME address/port while this one is listening
            // (two servers on one port, connections land in either). Windows
            // therefore keeps the default (exclusive) bind semantics so a busy
            // port fails loudly - which the desktop shell's port scan and the
            // single-instance check rely on.
            acceptor.set_option(asio::socket_base::reuse_address(true), ec);
#endif
            acceptor.bind(endpoint, ec);
            if (ec)
                throw boost::system::system_error(ec);
            acceptor.listen(asio::socket_base::max_listen_connections, ec);
            if (ec)
                throw boost::system::system_error(ec);
        }

        void run(rt::router &ros)
        {
            std::cout << "Server listening on " << address_ << ":" << port_ << "\n";
            co_spawn(handle, server_listener(ros), asio::detached);
            handle.run();
        }

        // stop accepting new requests and wait for the running handler
        // threads (SSE streams included) to finish
        void stop_handlers(int timeout_ms)
        {
            registry_->stop();
            registry_->wait_idle(timeout_ms);
        }

    private:
        asio::awaitable<void> server_listener(rt::router &ros)
        {
            for (;;)
            {
                boost::system::error_code ec;
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::redirect_error(asio::use_awaitable, ec));
                if (ec)
                {
                    if (registry_->stopping_now())
                        co_return;
                    continue;
                }
                co_spawn(socket.get_executor(),
                         std::make_shared<Session>(std::move(socket), max_buf_, registry_)->start(ros),
                         asio::detached);
            }
        }

        asio::io_context &handle;
        asio::ip::tcp::acceptor acceptor;
        std::string address_;
        unsigned short port_;
        size_t max_buf_ = 0;
        std::shared_ptr<handler_registry> registry_ = std::make_shared<handler_registry>();
    };
} // namespace servic

#endif //!__SERVER__H__
