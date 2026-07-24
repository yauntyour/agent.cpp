#pragma once
#ifndef __SERVER__H__
#define __SERVER__H__
#include <iostream>
#include <string>
#include <memory>
#include <coroutine>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "external/servic/router/router.hpp"

namespace servic
{
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
            return url;
        }

        // 解析失败（格式不对）
        return "";
    }

    size_t get_content_length(const std::string &header)
    {
        std::string content_length_header = "Content-Length: ";
        size_t pos = header.find(content_length_header);
        if (pos != std::string::npos)
        {
            size_t start = pos + content_length_header.length();
            size_t end = header.find("\r\n", start);
            if (end != std::string::npos)
            {
                std::string content_length_str = header.substr(start, end - start);
                return std::stoi(content_length_str);
            }
        }
        return 0;
    }

    namespace asio = boost::asio;
    class Session : public std::enable_shared_from_this<Session>
    {
    private:
        asio::ip::tcp::socket socket;
        size_t max_buf = 0;

    public:
        explicit Session(asio::ip::tcp::socket socket, size_t max_buf = 300000)
            : socket(std::move(socket)), max_buf(max_buf) {}
        asio::awaitable<void> start(rt::router &ros)
        {
            asio::co_spawn(socket.get_executor(), [self = shared_from_this(), &ros]() -> asio::awaitable<void>
                           {
                                  try
                                  {
                                    asio::streambuf data(self->max_buf);
                                    co_await asio::async_read_until(self->socket,data, "\r\n\r\n", asio::use_awaitable);

                                    std::string header(asio::buffers_begin(data.data()),asio::buffers_end(data.data()));

                                    std::string url = extract_url(header);
                                    std::cout << "New connection from: "
                                            << self->socket.remote_endpoint().address().to_string()
                                            << ":" << self->socket.remote_endpoint().port() <<" on: " << url << std::endl;

                                    // 检查是否为流式路由
                                    if (ros.has_stream_handler(url))
                                    {
                                        auto handler = ros.get_stream_handler(url);
                                        if (handler)
                                        {
                                            size_t content_length = get_content_length(header);
                                            size_t header_size = header.find("\r\n\r\n") + 4;
                                            if (content_length > 0 && header.size() < header_size + content_length)
                                            {
                                                size_t body_size = content_length - (header.size() - header_size);
                                                asio::streambuf body(self->max_buf);
                                                co_await asio::async_read(self->socket, body, asio::transfer_exactly(body_size), asio::use_awaitable);
                                                header.append(asio::buffers_begin(body.data()), asio::buffers_end(body.data()));
                                            }
                                            // 创建写回调，绑定到当前 socket
                                            auto write_cb = [self](const std::string &chunk)
                                            {
                                                try
                                                {
                                                    asio::write(self->socket, asio::buffer(chunk));
                                                }
                                                catch (...) {}
                                            };
                                            std::map<std::string, std::string> empty_params;
                                            handler(header, write_cb, empty_params);
                                            self->socket.close();
                                            co_return;
                                        }
                                    }

                                    std::string buf;
                                    auto [ptr,params] = ros.get(url);
                                    if (ptr.expired())
                                    {
                                        buf = "HTTP/1.1 404 Not Found\r\n\r\n";
                                    }else
                                    {
                                        size_t content_length = get_content_length(header);
                                        size_t header_size = header.find("\r\n\r\n")+4;
                                        if (content_length > 0 && header.size() < header_size + content_length)
                                        {
                                            size_t body_size = content_length - (header.size() - header_size);
                                            asio::streambuf body(self->max_buf);
                                            co_await asio::async_read(self->socket,body, asio::transfer_exactly(body_size), asio::use_awaitable);
                                            header.append(asio::buffers_begin(body.data()),asio::buffers_end(body.data()));
                                        }
                                        auto locked_node = ptr.lock();
                                        if (!locked_node) {
                                            buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                                        } else {
                                            int err = locked_node->func(header, buf,params);
                                            if (err == rt::FLAG_ERROR)
                                            {
                                                buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                                            }
                                        }
                                    }
                                    self->socket.write_some(asio::buffer(buf));
                                    self->socket.close();
                                  }
                                  catch (const std::exception &e)
                                  {
                                      std::cerr <<"Disconnected with error:"<< e.what() << '\n';
                                  }
                                  co_return; }, asio::detached);
            co_return;
        }
    };
    class Server
    {
    private:
        asio::io_context &handle;
        asio::ip::tcp::acceptor acceptor;
        short port;
        size_t max_buf = 0;
        asio::awaitable<void> server_listener(asio::ip::tcp::acceptor &acceptor, rt::router &ros)
        {
            while (true)
            {
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                co_spawn(socket.get_executor(), std::make_shared<Session>(std::move(socket), max_buf)->start(ros), asio::detached);
            }
        }

    public:
        explicit Server(asio::io_context &io_context, short port, size_t max_buf = 300000)
            : handle(io_context),
              acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
              port(port),
              max_buf(max_buf)
        {
        }
        void run(rt::router &ros)
        {
            std::cout << "Server listening port:" << port << "\n";
            co_spawn(handle, server_listener(acceptor, ros), asio::detached);
            handle.run();
        }
    };
} // namespace servic

#endif //!__SERVER__H__