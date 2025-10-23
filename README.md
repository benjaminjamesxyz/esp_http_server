# ESP32 HTTP Server Component

A lightweight, efficient HTTP server component for ESP32 using ESP-IDF, written in modern C++23.

## Features

- Minimal and embeddable HTTP server optimized for the ESP32 platform
- Route handling with efficient FNV-1a hash-based lookup
- Fixed-size memory buffers to avoid dynamic allocation
- Connection timeout handling and error recovery
- Fully implemented in idiomatic C++23 with constexpr and string_view
- Designed for easy integration as an ESP-IDF component

## Requirements

- ESP-IDF v4.1.0 or newer
- C++23 compatible toolchain (ESP-IDF sets `-std=gnu++2b`)
- ESP32 target hardware

## Installation

Clone the repository into your ESP-IDF components directory:

```
cd $IDF_PATH/components
git clone https://github.com/benjaminjamesxyz/esp_http_server.git http_server
```

Include `http_server` in your `CMakeLists.txt` components list.

## Usage

### Adding Routes

```
http_server::HttpServer server;

server.add_route("/hello", http_server::HttpMethod::GET, [](netconn *conn, std::string_view req) {
    // Handle GET /hello
    // Build and send HttpResponse here
    return 0;
});

server.start();
```

### Running Server

Call `server.start()` to start the listener task.

## Contributing

Contributions, bug reports, and feature requests are welcome! Please open issues or pull requests.

## License

MIT License - see the [`LICENSE`](LICENSE) file for details.

---

Built with ESP-IDF and modern C++23 for embedded systems enthusiasts.
