#pragma once
#include <string>
#include <vector>
#include <cstddef>
#include <iostream>


// Convert a std::string to a std::vector<std::byte>
inline std::vector<std::byte> string_to_bytes(const std::string& s) {
    return std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(s.data()),
        reinterpret_cast<const std::byte*>(s.data()) + s.size()
    );
}

// Trim leading and trailing whitespace from a string
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t");
    auto end = s.find_last_not_of(" \t");
    return start == std::string::npos ? "" : s.substr(start, end - start + 1);
}

inline void print_help() {
    std::cout << "============================\n";
    std::cout << "  WebSocket CLI Client\n";
    std::cout << "============================\n";
    std::cout << "Available commands:\n";
    std::cout << "  connect [host] [port] [path]   - Connect to a server(default: echo.websocket.org)\n";
    std::cout << "  send_text <message>            - Send a text message\n";
    std::cout << "  send_binary <message>          - Send a binary message\n";
    std::cout << "  ping [<message>]               - Send a ping frame\n";
    std::cout << "  pong [<message>]               - Send a pong frame\n";
    std::cout << "  close [<message>]              - Close the connection\n";
    std::cout << "  help                           - Show this help message\n";
    std::cout << "  exit / quit                    - Exit the program\n";
    std::cout << "============================\n";
}

