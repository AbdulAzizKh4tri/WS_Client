# C++ WebSocket Client (GN Build System)

## Overview

This project is a C++ implementation of a WebSocket client built from the protocol level up, without using high-level WebSocket libraries.  
It demonstrates:

- Understanding of the WebSocket protocol (RFC 6455)
- Layered network abstractions (TCP → TLS → WebSocket)
- Manual frame parsing, masking, and fragmentation handling
- Use of the GN (Generate Ninja) build system
- Cross-compiler support (GCC and Clang)
- Basic unit testing

The client can connect to public WebSocket echo servers, send messages, and receive echoed responses.

---

## Features

### WebSocket Client
- Manual HTTP → WebSocket upgrade handshake
- RFC 6455–compliant frame parsing
- Support for:
  - Text frames
  - Binary frames
  - Ping / Pong
  - Close frames
- Fragmentation handling (FIN = 0 / FIN = 1)
- Graceful handling of connection errors and shutdowns

### Transport Layer
- Plain TCP (`ws://`)
- Secure WebSocket over TLS (`wss://`) using Asio + OpenSSL
- TLS attempted first for secure URLs, with optional fallback to plain TCP

### Command-Line Interface
- Simple interactive CLI for sending messages
- URL-based connection to WebSocket servers
- Built using CLI11

### Build System
- GN meta-build system with Ninja backend
- Separate configurations:
  - Debug / Release
  - GCC / Clang
- Explicit compiler flags and toolchain definitions

### Testing
- Unit tests for critical components
- Tests integrated as GN targets

---

## Project Structure

├── build

│   ├── BUILDCONFIG.gn

│   ├── BUILD.gn

│   └── toolchain

│       └── BUILD.gn

├── BUILD.gn

├── .gn

├── src

│   ├── client.cpp

│   ├── TcpConnection.hpp

│   ├── utils.hpp

│   └── WebSocket.hpp

├── tests

│   └── websocket_test.cpp

└── third_party

    ├── asio
    
    │   └── include
    
    ├── Catch2
    
    │   ├── catch_amalgamated.cpp
    
    │   └── catch_amalgamated.hpp
    
    └── CLI11
        └── include


Each layer is intentionally separated to avoid mixing transport, protocol, and user-interface logic.

---

## WebSocket Protocol Notes

- The client performs a full HTTP upgrade handshake as specified in RFC 6455.
- A random `Sec-WebSocket-Key` (16 random bytes, base64-encoded) is generated for each connection.
- Frame parsing handles:
  - FIN bit
  - Relevant opcode decoding
  - Masked payloads
  - Extended payload lengths (126 / 127)
- Fragmented frames are buffered until a final frame (`FIN = 1`) completes the message.

---

## Build Instructions

### Requirements
- GN
- Ninja
- OpenSSL development libraries
- Asio
- CLI11 (included as a dependency)

### Generate Build Files

Builds using is_debug and use_clang flags

gn gen out/debug --args='is_debug=true'
gn gen out/clang_debug --args='
is_debug=true
use_clang=true
'
gn gen out/release
gn gen out/clang_release --args='use_clang=true'

### Build

ninja -C out/< directory >

### Running the client

./out/< directory >/client

## Design Decisions

- No high-level WebSocket libraries were used to demonstrate protocol-level understanding.
- TLS is layered beneath the WebSocket protocol to keep the implementation transport-agnostic.
- Code is organized to favor clarity and separation of concerns over minimal file count.

---

## Possible Improvements

Given more time, the following could be added:

- More exhaustive handshake validation
- Expanded test coverage for edge cases
- Full continuation opcode handling
