#pragma once

#include <thread>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unordered_map>
#include <chrono>
#include "http.hpp"
#include "router.hpp"

constexpr static int MaxBufferSize = 10000;

struct EventInfo {
    EventInfo() : fd(0), read(0), total(0), buffer() {}
    int fd;
    int read;
    int total;
    char buffer[MaxBufferSize];
};

class HttpServer {
private:
    constexpr static int ThreadPoolSize = 5;
    constexpr static int MaxEventSize = 10000;

    std::uint16_t port_;
    std::string host_;
    int socket_fd_;
    sockaddr_in server_info_;
    bool active_;
    Router router_;

    EventInfo* handle_httpdata(EventInfo *data);
    void server_listen(void);
    void process_epoll_event(int epfd, EventInfo *data, epoll_event ev);
    void process_event(int worker_id);
    void handle_epoll_ctrl(int epfd, int op, int fd, void* = nullptr, uint32_t event_type = 0);
    void setup_server();

    int worker_epoll_fd_[ThreadPoolSize];
    struct epoll_event worker_events_[ThreadPoolSize][MaxEventSize];
    std::thread listen_thread_;
    std::thread worker_thread_[ThreadPoolSize];

public:
    explicit HttpServer(const std::string& host, const int port);
    ~HttpServer() = default;

    HttpServer() = default;
    HttpServer(HttpServer&&) = default;

    void start();
    void stop();
    void add(const std::string& method, const std::string& pattern, const route_handler& call_back);
    std::uint16_t get_port() const {return port_;}
    std::string get_host() const {return host_;}
    int get_sockfd() const {return socket_fd_;}
};