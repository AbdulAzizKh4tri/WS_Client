#define CATCH_CONFIG_MAIN
#include "catch_amalgamated.hpp"

#include <vector>
#include <memory>
#include <cstddef>
#include <cstring>

#include "WebSocket.hpp"

/*
    Captures callbacks set by WebSocket
    Allows tests to inject raw bytes as if they came from the network
    No real I/O
*/
class DummyConnection : public TcpConnection {
public:
    DummyConnection()
        : TcpConnection(dummy_io_, "dummy", "0") {}

    void send(std::vector<std::byte> data) override {
        sent_frames.push_back(std::move(data));
    }

    void trigger_connected() {
        if (on_connect_)
            on_connect_(false);
    }

    void inject(const std::vector<std::byte>& bytes) {
        if (on_data_)
            on_data_(bytes.data(), bytes.size());
    }

    std::vector<std::vector<std::byte>> sent_frames;

private:
    static asio::io_context dummy_io_;
};

asio::io_context DummyConnection::dummy_io_;

/*---------
   Helpers
----------*/

static std::vector<std::byte> bytes(const std::string& s) {
    return {
        reinterpret_cast<const std::byte*>(s.data()),
        reinterpret_cast<const std::byte*>(s.data()) + s.size()
    };
}

static std::vector<std::byte> text_frame(
    const char* s,
    bool fin = true)
{
    size_t len = std::strlen(s);

    std::vector<std::byte> f;
    f.push_back(std::byte((fin ? 0x80 : 0x00) | 0x1)); // FIN + text
    f.push_back(std::byte(len));
    for (size_t i = 0; i < len; ++i)
        f.push_back(std::byte(s[i]));
    return f;
}

/* -----
   Tests
-------- */

TEST_CASE("WebSocket parses a single text frame")
{
    auto conn = std::make_shared<DummyConnection>();
    WebSocket ws(conn, "x", "80", "/");

    std::vector<std::byte> received;
    ws.on_message([&](const auto& msg) {
        received = msg;
    });

    conn->trigger_connected();
    // handshake response to switch to Open state
    conn->inject(bytes("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n"));
    conn->inject(text_frame("hello"));

    REQUIRE(received.size() == 5);
    REQUIRE(std::memcmp(received.data(), "hello", 5) == 0);
}

TEST_CASE("WebSocket parses fragmented text frames")
{
    auto conn = std::make_shared<DummyConnection>();
    WebSocket ws(conn, "x", "80", "/");

    std::vector<std::byte> received;
    ws.on_message([&](const auto& msg) {
        received = msg;
    });

    conn->trigger_connected();
    conn->inject(bytes("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n"));

    conn->inject(text_frame("hel", false)); // FIN = 0, add to message buffer
    conn->inject(text_frame("lo", true));  // FIN = 1 responde with message

    REQUIRE(received.size() == 5);
    REQUIRE(std::memcmp(received.data(), "hello", 5) == 0);
}

TEST_CASE("WebSocket replies to ping with pong")
{
    auto conn = std::make_shared<DummyConnection>();
    WebSocket ws(conn, "x", "80", "/");

    conn->trigger_connected();
    conn->inject(bytes("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n"));

    std::vector<std::byte> ping = {
        std::byte{0x89}, // FIN + ping
        std::byte{0x02},
        std::byte{'h'}, std::byte{'i'}
    };

    conn->inject(ping);

    REQUIRE(conn->sent_frames.size() == 2);  // Handshake + pong

    const auto& pong = conn->sent_frames[1];  // Pong is the second frame

    REQUIRE(pong.size() == 8);
    REQUIRE(uint8_t(pong[0]) == 0x8A);
    REQUIRE(uint8_t(pong[1]) == 0x82);

    // Extract mask
    std::array<std::byte, 4> mask = {pong[2], pong[3], pong[4], pong[5]};

    // Check unmasked payload
    REQUIRE((pong[6] ^ mask[0]) == std::byte{'h'});
    REQUIRE((pong[7] ^ mask[1]) == std::byte{'i'});
}

TEST_CASE("WebSocket emits close on close frame")
{
    auto conn = std::make_shared<DummyConnection>();
    WebSocket ws(conn, "x", "80", "/");

    bool closed = false;
    ws.on_close([&](const auto&) {
        closed = true;
    });

    conn->trigger_connected();
    conn->inject(bytes("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n"));

    std::vector<std::byte> close = {
        std::byte{0x88}, // FIN + close
        std::byte{0x00}
    };

    conn->inject(close);

    REQUIRE(closed == true);
}
