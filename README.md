# HTTP Server

High-performance HTTP server built from scratch in C++17.
Handles 110,000 requests/second with 10,000 concurrent connections.

## Architecture
- epoll-based event loop
- Thread pool (N worker threads, each with own epoll instance)
- Regex-based router with dynamic path parameters
- Middleware chain (logger, timer, auth)

## Benchmark
| Connections | Req/sec   | Latency |
|-------------|-----------|---------|
| 100         | 74,541    | 1.65ms  |
| 1,000       | 105,489   | 9.27ms  |
| 10,000      | 119,161   | 9.56ms  |

## Build

### Requirements
- **OS**: Linux (epoll is Linux-only)
- **Compiler**: GCC 11.4+ or equivalent
- **CMake**: 3.22+
- **C++ Standard**: C++17

### Using Docker
```bash
docker build -t http-server .
docker run -it -v $(pwd):/app -p 8080:8080 http-server bash
cd /app && mkdir build && cd build
cmake .. && make
```

## Usage

### Start the server
```bash
./server
```

### Register routes in main.cpp
```cpp
HttpServer server("0.0.0.0", 8080);

server.use([](const HttpRequest* req, HttpRespond* res, next_func next) {
    cout << req->method << " " << req->path << endl;
    next();
});

server.add("GET", "/posts/:id", [](const HttpRequest* req, HttpRespond* res, auto params) {
    res->status_code = 200;
    res->body = "{\"id\": \"" + params["id"] + "\"}";
});

server.start();
```

### Test endpoints
```bash
curl http://localhost:8080/
curl http://localhost:8080/posts/42
wrk -t4 -c100 -d10s http://localhost:8080/
```