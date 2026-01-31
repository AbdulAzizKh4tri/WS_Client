#include <iostream>
#include <string>
#include <sstream>
#include <memory>
#include <vector>
#include <asio.hpp>

#include "TcpConnection.hpp"
#include "WebSocket.hpp"

#include "utils.hpp"

// -------------------- CLI --------------------
int main() {
    print_help();

    asio::io_context io;
    auto work = asio::make_work_guard(io);
    std::thread io_thread([&]{ io.run(); });

    std::shared_ptr<TcpConnection> conn;
    std::shared_ptr<WebSocket> ws;
    bool connected = false;

    std::string line;
    while (true) {
        if (!std::getline(std::cin, line)) {
            std::cout << "\n[EOF] shutting down\n";
            if (connected && ws) ws->send_close();
            break;
        }

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "connect") {
            std::string host="echo.websocket.org", port = "443", path = "/";
            iss >> host >> port >> path;

            conn = std::make_shared<TcpConnection>(io, host, port);
            ws = std::make_shared<WebSocket>(conn, host, port, path);

            ws->on_open([&]{ 
                connected = true;
                std::cout << "[WebSocket Opened]\n"; 
            });

            ws->on_binary([](const std::vector<std::byte>& data){
                std::cout << "================= [Server Response] =================" << std::endl;
				for (auto b : data) {
					std::cout << (int)b << " ";
				}
                std::cout << std::endl << "=================  [Response Ends]  =================" << std::endl;

            });

            ws->on_message([](const std::vector<std::byte>& data){
                std::string msg(reinterpret_cast<const char*>(data.data()), data.size());
                std::cout << "[Server] " << msg << std::endl;
            });

            ws->on_ping([](const std::vector<std::byte>&){ 
                std::cout << "[Received PING]\n"; 
            });

            ws->on_pong([](const std::vector<std::byte>&){ 
                std::cout << "[Received PONG]\n"; 
            });

            ws->on_close([&](const std::vector<std::byte>&){ 
                connected = false;
                std::cout << "[Connection Closed]\n"; 
            });

            ws->on_error([](const std::string& err){ 
                std::cerr << "[Error] " << err << "\n"; 
            });
        }
        else if (cmd == "exit" || cmd == "quit") {
            if (connected) ws->send_close();
            break;
        }
        else if (cmd == "help" || cmd == "?") {
            print_help();
        }
        else if (connected) {
            if (cmd == "send_text") {
                std::string msg;
                std::getline(iss, msg);
                ws->send_text(trim(msg));
            }
            else if (cmd == "send_binary") {
                std::string msg;
                std::getline(iss, msg);
                ws->send_binary(string_to_bytes(trim(msg)));
            }
            else if (cmd == "ping") {
                std::string msg;
                std::getline(iss, msg);
                ws->send_ping(string_to_bytes(trim(msg)));
            }
            else if (cmd == "pong") {
                std::string msg;
                std::getline(iss, msg);
                ws->send_pong(string_to_bytes(trim(msg)));
            }
            else if (cmd == "close") {
                std::string reason;
                std::getline(iss, reason);
                ws->send_close(string_to_bytes(trim(reason)));
            }else{
                std::cout << "Unknown command: " << cmd << "\n";
                std::cout << "Type `help` to see available commands.\n";
            }
        } else if(cmd == "send_text" || cmd == "send_binary" || cmd == "ping" || cmd == "pong" || cmd == "close"){
            std::cout << "Not connected! Use `connect` first.\n";
        }
        else {
            std::cout << "Unknown command: " << cmd << "\n";
            std::cout << "Type `help` to see available commands.\n";
        }
    }

    std::cout << "[client] closed\n";
    work.reset();
    io_thread.join();
    return 0;
}
