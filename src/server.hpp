#pragma once

#include <thread>
#include <iostream>
#include <fstream>
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
#include <mutex>
#include "http.hpp"
#include "router.hpp"

constexpr static int MaxBufferSize = 10000;

using next_func = std::function<void()>;
using middleware = std::function<void(const HttpRequest*, HttpRespond*, next_func)>;

struct EventInfo {
    EventInfo() : fd(0), write_cursor(0), keep_alive(true), last_active(std::chrono::steady_clock::now()) {}
    int fd;
    int write_cursor;
    bool keep_alive;
    std::chrono::steady_clock::time_point last_active;
    std::string read_buffer;
    std::string write_buffer;
};

class HttpServer {
private:
    constexpr static int ThreadPoolSize = 5;
    constexpr static int MaxEventSize = 10000;

    std::uint16_t port_;
    std::string host_;
    std::string static_dir_;
    int socket_fd_;
    sockaddr_in server_info_;
    bool active_;
    Router router_;
    std::mutex worker_mutex[ThreadPoolSize];
    std::vector<middleware> middlewares_;
    std::vector<EventInfo*> worker_connections_[ThreadPoolSize];

    void execution_chain(int index, const HttpRequest* req, HttpRespond* res, 
    const route_handler& handler, std::unordered_map<std::string, std::string>& params);
    bool check_path_exist(const std::string& path);
    bool check_path_legal(const std::string& path);
    std::string get_content_type(const std::string &path);
    EventInfo* handle_httpdata(EventInfo *data);
    void server_listen(void);
    void process_epoll_event(int epfd, EventInfo *data, epoll_event ev, int worker_id);
    void process_event(int worker_id);
    void delete_fd(int fd, int worker_id);
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
    void use(middleware middle);
    void set_static_dir(const std::string& dir) { static_dir_ = dir; }
    void serve_file(const HttpRequest* req, HttpRespond* res);
    void add(const std::string& method, const std::string& pattern, const route_handler& call_back);
    std::uint16_t get_port() const {return port_;}
    std::string get_host() const {return host_;}
    int get_sockfd() const {return socket_fd_;}
};