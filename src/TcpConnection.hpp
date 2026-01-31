#pragma once

#include <asio.hpp>
#include <asio/ssl.hpp>
#include <iostream>
#include <string>
#include <functional>
#include <array>
#include <memory>
#include <vector>

using asio::ip::tcp;

class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
    using DataHandler =
        std::function<void(const std::byte* data, std::size_t size)>;

    using ErrorHandler =
        std::function<void(const asio::error_code&)>;

    using ConnectHandler =
        std::function<void(bool /* ssl */)>;

    TcpConnection(asio::io_context& io,
                  std::string host,
                  std::string port)
        : io_(io),
          host_(std::move(host)),
          port_(std::move(port)),
          resolver_(io_),
          socket_(io_),
          ssl_ctx_(asio::ssl::context::tls_client),
          ssl_stream_(socket_, ssl_ctx_),
          write_strand_(asio::make_strand(io_))  // strand initialized here
    {
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(asio::ssl::verify_peer);
    }

    void on_data(DataHandler h)       { on_data_ = std::move(h); }
    void on_error(ErrorHandler h)     { on_error_ = std::move(h); }
    void on_connect(ConnectHandler h) { on_connect_ = std::move(h); }

    void start()
    {
        // Resolve host:port and attempt secure connection
        auto self = shared_from_this();
        resolver_.async_resolve(host_, port_,
            [this, self](const asio::error_code& ec,
                         const tcp::resolver::results_type& endpoints)
            {
                if (ec) return fail(ec);
                try_secure_connect(endpoints);
            });
    }

    virtual void send(std::vector<std::byte> data) // void* data, size_t
    {
        auto self = shared_from_this();

        asio::post(write_strand_,
            [this, self, data = std::move(data)]() mutable
            {
                auto buffer = asio::buffer(data);

                auto handler =
                    [this, self, data = std::move(data)]
                    (const asio::error_code& ec, std::size_t)
                    {
                        if (ec) fail(ec);
                    };

                if (use_ssl_)
                    asio::async_write(ssl_stream_, buffer, handler);
                else
                    asio::async_write(socket_, buffer, handler);
            });
    }

protected:
    DataHandler on_data_;
    ErrorHandler on_error_;
    ConnectHandler on_connect_;

private:
    
    // attempt secure connection first, if it fails, attempt insecure connect /plain connect
    void try_secure_connect(const tcp::resolver::results_type& endpoints)
    {
        auto self = shared_from_this();

        asio::async_connect(socket_, endpoints,
            [this, self, endpoints](const asio::error_code& ec, const tcp::endpoint&)
            {
                if (ec) return fail(ec);

                if (!SSL_set_tlsext_host_name(
                        ssl_stream_.native_handle(), host_.c_str()))
                {
                    socket_.close();
                    return try_plain_connect(endpoints);
                }

                ssl_stream_.async_handshake(
                    asio::ssl::stream_base::client,
                    [this, self, endpoints](const asio::error_code& ec)
                    {
                        if (ec)
                        {
                            socket_.close();
                            return try_plain_connect(endpoints);
                        }

                        use_ssl_ = true;
                        if (on_connect_) on_connect_(true);
                        start_read();
                    });
            });
    }

    // start connection and start listening data
    void try_plain_connect(const tcp::resolver::results_type& endpoints)
    {
        auto self = shared_from_this();

        asio::async_connect(socket_, endpoints,
            [this, self](const asio::error_code& ec, const tcp::endpoint&)
            {
                if (ec) return fail(ec);

                use_ssl_ = false;
                if (on_connect_) on_connect_(false);
                start_read();
            });
    }

    void start_read()
    {
        auto self = shared_from_this();
        auto buf = std::make_shared<std::array<std::byte, 4096>>();

        auto handler =
            [this, self, buf](const asio::error_code& ec, std::size_t n)
            {
                if (ec == asio::error::eof ||
                    ec == asio::ssl::error::stream_truncated)
                    return;

                if (ec) return fail(ec);

                if (on_data_)
                    on_data_(buf->data(), n);

                start_read();
            };

        if (use_ssl_)
            ssl_stream_.async_read_some(asio::buffer(*buf), handler);
        else
            socket_.async_read_some(asio::buffer(*buf), handler);
    }

    void fail(const asio::error_code& ec)
    {
        if (on_error_) on_error_(ec);
    }

    asio::io_context& io_;
    std::string host_;
    std::string port_;

    tcp::resolver resolver_;
    tcp::socket socket_;

    asio::ssl::context ssl_ctx_;
    asio::ssl::stream<tcp::socket&> ssl_stream_;

    asio::strand<asio::io_context::executor_type> write_strand_;  // strand protects send()

    bool use_ssl_{false};
};
