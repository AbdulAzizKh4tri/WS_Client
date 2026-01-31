#pragma once

#include <random>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include "TcpConnection.hpp"
#include <openssl/rand.h>
#include <openssl/evp.h>

enum class State
{
    Connecting,
    HttpHandshake,
    Open,
    Closing,
    Closed,
    Error
};

enum class ws_opcode : uint8_t {
    text   = 0x1,
    binary = 0x2,
    close  = 0x8,
    ping   = 0x9,
    pong   = 0xA
};

class WebSocket
{
public:
    using MessageHandler = std::function<void(const std::vector<std::byte>&)>;
    using BinaryHandler = std::function<void(const std::vector<std::byte>&)>;
    using ErrorHandler   = std::function<void(const std::string&)>;
    using OpenHandler    = std::function<void()>;
    using PingHandler  = std::function<void(const std::vector<std::byte>&)>;
    using PongHandler  = std::function<void(const std::vector<std::byte>&)>;
    using CloseHandler = std::function<void(const std::vector<std::byte>&)>;

    explicit WebSocket(std::shared_ptr<TcpConnection> conn,
                       const std::string& host, 
                       const std::string& port, 
                       const std::string& path)
        : conn_(std::move(conn)),
          host_(host),
          port_(port),
          path_(path)
    {
        conn_->on_connect([this](bool ssl){

            if(ssl)
                std::cout<<"TCP connection made with SSL"<<std::endl;
            else
                std::cout<<"TCP connection made without SSL"<<std::endl;


            std::string req =
                "GET " + path_ + " HTTP/1.1\r\n"
                "Host: " + host_ + ":" + port_ + "\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: "+ get_secret_key() +"\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n";

            state_ = State::HttpHandshake;

            std::vector<std::byte> payload(
                reinterpret_cast<const std::byte*>(req.data()),
                reinterpret_cast<const std::byte*>(req.data() + req.size())
            );
            conn_->send(std::move(payload));

        });

        conn_->on_data([this](const void* data, std::size_t size){
            if (state_ == State::HttpHandshake)
                handle_handshake_data(data, size);
            else if (state_ == State::Open)
                handle_frame_data(data, size);
        });

        conn_->on_error([](const std::error_code& ec){
            std::cerr << "TCP Error: " << ec.message() << "\n";
        });

        conn_->start();
    }

    void send_text(std::string text) {
        std::vector<std::byte> payload(
            reinterpret_cast<std::byte*>(text.data()),
            reinterpret_cast<std::byte*>(text.data() + text.size())
        );
        send_frame(ws_opcode::text, std::move(payload));
    }

    void send_binary(std::vector<std::byte> payload) {
        send_frame(ws_opcode::binary, std::move(payload));
    }

    void send_ping(const std::vector<std::byte> payload = {}) {
        send_frame(ws_opcode::ping, std::move(payload));
    }

    void send_pong(const std::vector<std::byte> payload = {}) {
        send_frame(ws_opcode::pong, std::move(payload));
    }

    void send_close(const std::vector<std::byte> payload = {}) {
        if (state_ == State::Closing || state_ == State::Closed) return;

        send_frame(ws_opcode::close, std::move(payload));
        state_ = State::Closing;
        if(on_close_) on_close_(payload);
    }

    void on_message(MessageHandler h) { on_message_ = std::move(h); }
    void on_binary(BinaryHandler h) { on_binary_ = std::move(h); }
    void on_error(ErrorHandler h)     { on_error_ = std::move(h); }
    void on_open(OpenHandler h)       { on_open_ = std::move(h); }
    void on_ping(PingHandler h)       { on_ping_ = std::move(h); }
    void on_pong(PongHandler h)       { on_pong_ = std::move(h); }
    void on_close(CloseHandler h)     { on_close_ = std::move(h); }

private:

    std::string get_secret_key() {
        unsigned char buf[16], out[24];
        if (RAND_bytes(buf, sizeof(buf)) != 1)
            throw std::runtime_error("RAND_bytes failed");
        EVP_EncodeBlock(out, buf, sizeof(buf));
        return std::string(reinterpret_cast<char*>(out), sizeof(out));
    }

    void handle_handshake_data(const void* data, std::size_t size){
        const auto* bytes = static_cast<const std::byte*>(data);

        // add everythng to response_buffer
        response_buffer_.insert(response_buffer_.end(), bytes, bytes + size);

        // go to http header end "\r\n\r\n", if not found, storing in response buffer is enough 
        auto it = std::search(response_buffer_.begin(), response_buffer_.end(),
                              http_end.begin(), http_end.end());
        if (it == response_buffer_.end()) return;

        // get header as string to parse
        auto header_len = std::distance(response_buffer_.begin(), it) + http_end.size();
        std::string headers_str(reinterpret_cast<const char*>(response_buffer_.data()), header_len);

        // if not expected header, just throw an error and let the upper level handle it
        if (headers_str.find("101 Switching Protocols") == std::string::npos) {
            state_ = State::Error;
            if (on_error_) on_error_("Handshake Failed:\r\n" + headers_str);
            response_buffer_.clear();
            return;
        }

        //header parsed, if there is any frame data that came along, start adding that to frame_buffer
        auto body_start = it + http_end.size();
        if (body_start != response_buffer_.end())
            frame_buffer_.insert(frame_buffer_.end(), body_start, response_buffer_.end());

        // this response is consumed, clear it and declare socket open
        response_buffer_.clear();
        state_ = State::Open;

        if (on_open_) on_open_();
        parse_frames();
    }

    void handle_frame_data(const void* data, std::size_t size){
        // add to frame_buffer and try to parse
        const auto* bytes = static_cast<const std::byte*>(data);
        frame_buffer_.insert(frame_buffer_.end(), bytes, bytes + size);
        parse_frames();
    }

    void parse_frames() {
        while (try_parsing_one_frame()) {}
    }

    bool try_parsing_one_frame() {
        if (frame_buffer_.size() < 2) return false;

        const uint8_t b0 = uint8_t(frame_buffer_[0]);
        const uint8_t b1 = uint8_t(frame_buffer_[1]);

        bool fin    = b0 & 0x80;
        uint8_t op  = b0 & 0x0F;
        bool masked = b1 & 0x80;
        uint64_t len = b1 & 0x7F;

        size_t header_len = 2;

        if (len == 126) {
            if (frame_buffer_.size() < 4) return false;
            len = (uint8_t(frame_buffer_[2]) << 8) |
                  uint8_t(frame_buffer_[3]);
            header_len = 4;
        } else if (len == 127) {
            if (frame_buffer_.size() < 10) return false;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | uint8_t(frame_buffer_[2 + i]);
            header_len = 10;
        }

        if (masked) header_len += 4;
        if (frame_buffer_.size() < header_len + len) return false;

        // get payload
        std::vector<std::byte> payload(
            frame_buffer_.begin() + header_len,
            frame_buffer_.begin() + header_len + len
        );

        // unmask
        if (masked) {
            std::array<std::byte, 4> mask;
            size_t mask_start = header_len - 4;
            for (int i = 0; i < 4; ++i) {
                mask[i] = frame_buffer_[mask_start + i];
            }
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % 4];
            }
        }

        // frame not needed since we have payload
        frame_buffer_.erase(frame_buffer_.begin(),
                            frame_buffer_.begin() + header_len + len);

        switch (static_cast<ws_opcode>(op)) {
            case ws_opcode::text:
                message_buffer_.insert(message_buffer_.end(),
                                    payload.begin(), payload.end());
                if (fin) {
                    if(on_message_) on_message_(message_buffer_);
                    message_buffer_.clear();
                }
                break;

            case ws_opcode::binary:
                message_buffer_.insert(message_buffer_.end(),
                                    payload.begin(), payload.end());
                if (fin) {
                    if(on_binary_) on_binary_(message_buffer_);
                    message_buffer_.clear();
                }
                break;

            case ws_opcode::ping:
                if(on_ping_) on_ping_(payload);
                send_pong(payload); // auto-reply
                break;

            case ws_opcode::pong:
                if(on_pong_) on_pong_(payload);
                break;

            case ws_opcode::close:
                if(on_close_) on_close_(payload);
                if(state_ != State::Closing) send_close(payload);
                state_ = State::Closed;
                break;

            default:
                break;
        }


        return true;
    }

    void send_frame(ws_opcode opcode, std::vector<std::byte> payload) {
        std::vector<std::byte> frame;

        //building header
        frame.push_back(std::byte(0x80 | uint8_t(opcode)));

        uint8_t mask_bit = masking_ ? 0x80 : 0x00;
        size_t len = payload.size();

        if (len <= 125) {
            frame.push_back(std::byte(mask_bit | len));
        } else if (len <= 65535) {
            frame.push_back(std::byte(mask_bit | 126));
            frame.push_back(std::byte((len >> 8) & 0xff));
            frame.push_back(std::byte(len & 0xff));
        } else {
            frame.push_back(std::byte(mask_bit | 127));
            for (int i = 7; i >= 0; --i)
                frame.push_back(std::byte((len >> (8 * i)) & 0xff));
        }

        if (masking_) {
            auto mask = generate_mask();
            // add mask to frame
            frame.insert(frame.end(), mask.begin(), mask.end());

            // mask the payload
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] ^= mask[i % 4];
        }

        // add the payload
        frame.insert(frame.end(),
                     std::make_move_iterator(payload.begin()),
                     std::make_move_iterator(payload.end()));

        conn_->send(std::move(frame));
    }

    std::array<std::byte, 4> generate_mask() {
        static std::random_device rd;
        std::array<std::byte, 4> mask;
        for (auto& b : mask) b = std::byte(rd() & 0xFF);
        return mask;
    }

private:
    static constexpr std::array<std::byte, 4> http_end = { std::byte{'\r'}, std::byte{'\n'},
                                                           std::byte{'\r'}, std::byte{'\n'} };

    std::shared_ptr<TcpConnection> conn_;
    std::string host_, port_, path_;
    bool masking_ = true;

    std::vector<std::byte> response_buffer_;
    std::vector<std::byte> frame_buffer_;
    std::vector<std::byte> message_buffer_;
    State state_ = State::Connecting;

    MessageHandler on_message_;
    BinaryHandler on_binary_;
    ErrorHandler on_error_;
    OpenHandler on_open_;
    PingHandler  on_ping_;
    PongHandler  on_pong_;
    CloseHandler on_close_;
};
