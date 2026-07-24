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

#ifdef AGENT_ENABLE_TLS
#include <boost/asio/ssl.hpp>
#endif

#include "external/servic/router/router.hpp"

namespace servic
{
    std::string extract_url(const std::string &raw_data)
    {
        if (raw_data.empty())
            return "";

        size_t line_end = raw_data.find_first_of("\r\n");
        std::string first_line;

        if (line_end == std::string::npos)
        {
            first_line = raw_data;
        }
        else
        {
            first_line = raw_data.substr(0, line_end);
        }

        std::istringstream iss(first_line);
        std::string method, url, version;

        if (iss >> method >> url >> version)
        {
            return url;
        }

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

#ifdef AGENT_ENABLE_TLS
    using ssl_context = asio::ssl::context;
    using ssl_socket = asio::ssl::stream<asio::ip::tcp::socket>;

    class Session : public std::enable_shared_from_this<Session>
    {
    private:
        ssl_socket socket;
        size_t max_buf = 0;

    public:
        explicit Session(ssl_socket socket, size_t max_buf = 300000)
            : socket(std::move(socket)), max_buf(max_buf) {}

        asio::awaitable<void> start(rt::router &ros)
        {
            auto self = shared_from_this();
            try
            {
                co_await socket.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);

                asio::streambuf data(self->max_buf);
                co_await asio::async_read_until(self->socket, data, "\r\n\r\n", asio::use_awaitable);

                std::string header(asio::buffers_begin(data.data()), asio::buffers_end(data.data()));

                std::string url = extract_url(header);
                std::cout << "New TLS connection from: "
                          << self->socket.lowest_layer().remote_endpoint().address().to_string()
                          << ":" << self->socket.lowest_layer().remote_endpoint().port()
                          << " on: " << url << std::endl;

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
                        self->socket.lowest_layer().close();
                        co_return;
                    }
                }

                std::string buf;
                auto [ptr, params] = ros.get(url);
                if (ptr.expired())
                {
                    buf = "HTTP/1.1 404 Not Found\r\n\r\n";
                }
                else
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
                    auto locked_node = ptr.lock();
                    if (!locked_node)
                    {
                        buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                    }
                    else
                    {
                        int err = locked_node->func(header, buf, params);
                        if (err == rt::FLAG_ERROR)
                        {
                            buf = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
                        }
                    }
                }
                asio::write(self->socket, asio::buffer(buf));
                self->socket.lowest_layer().close();
            }
            catch (const std::exception &e)
            {
                std::cerr << "TLS session error: " << e.what() << '\n';
            }
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
        std::unique_ptr<ssl_context> ssl_ctx;
        bool tls_enabled = false;

        asio::awaitable<void> server_listener(asio::ip::tcp::acceptor &acceptor, rt::router &ros)
        {
            while (true)
            {
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                if (tls_enabled && ssl_ctx)
                {
                    ssl_socket ssl_sock(std::move(socket), *ssl_ctx);
                    co_spawn(ssl_sock.lowest_layer().get_executor(),
                             std::make_shared<Session>(std::move(ssl_sock), max_buf)->start(ros),
                             asio::detached);
                }
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

        void enable_tls(const std::string &cert_file, const std::string &key_file)
        {
            ssl_ctx = std::make_unique<ssl_context>(ssl_context::tlsv12_server);
            ssl_ctx->set_options(
                ssl_context::default_workarounds |
                ssl_context::no_sslv2 |
                ssl_context::no_sslv3 |
                ssl_context::single_dh_use);
            ssl_ctx->use_certificate_chain_file(cert_file);
            ssl_ctx->use_private_key_file(key_file, ssl_context::pem);
            tls_enabled = true;
            std::cout << "TLS enabled with cert: " << cert_file << ", key: " << key_file << "\n";
        }

        void run(rt::router &ros)
        {
            std::string proto = tls_enabled ? "HTTPS" : "HTTP";
            std::cout << proto << " Server listening port:" << port << "\n";
            co_spawn(handle, server_listener(acceptor, ros), asio::detached);
            handle.run();
        }
    };

#else
    // Non-TLS plain HTTP server
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
            std::cout << "HTTP Server listening port:" << port << "\n";
            co_spawn(handle, server_listener(acceptor, ros), asio::detached);
            handle.run();
        }
    };
#endif

} // namespace servic

#endif //!__SERVER__H__