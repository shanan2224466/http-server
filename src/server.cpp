#include "server.hpp"

using namespace std;

HttpRespond handleRequest(const HttpRequest& req) {
    HttpRespond res;
    if (req.method == "GET" && req.path == "/") {
        res.status_code = 200;
        res.status_text = "OK";
        res.body = "{\"message\":\"Welcome to the blog API\"}";
    }
    else if (req.method == "GET" && req.path == "/api/posts") {
        res.status_code = 200;
        res.status_text = "OK";
        res.body = "[{\"id\":1,\"title\":\"First Post\"}]";
    }
    else {
        res.status_code = 404;
        res.status_text = "Not Found";
        res.body = "{\"error\":\"Not Found\"}";
    }
    return res;
}

HttpRequest parseRequest(const string& raw) {
    HttpRequest r;
    stringstream ss(raw);
    ss >> r.method >> r.path >> r.version;
    return r;
}

HttpServer::HttpServer(const std::string& host, const int port)
    : port_(port)
    , host_(host)
    , socket_fd_(0)
    , server_info_()
    , active_(true)
    {}

void HttpServer::handle_epoll_ctrl(int epfd, int op, int fd, void* info, uint32_t event_type) {
    if (op == EPOLL_CTL_DEL) {
        if (epoll_ctl(epfd, op, fd, NULL) < 0) {
            cerr << "epoll ctrl error: " << strerror(errno) << endl;
        }
    }
    else {
        epoll_event event;
        event.data.ptr = info;
        event.events = event_type;
        if (epoll_ctl(epfd, op, fd, &event) < 0) {
            cerr << "epoll ctrl error: " << strerror(errno) << endl;
        }
    }
}

void HttpServer::server_listen() {
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int current_worker = 0;

    while (active_) {
        int client_fd = accept(socket_fd_, (sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
            continue;
        }

        EventInfo *client = new EventInfo();
        client->fd = client_fd;
        handle_epoll_ctrl(worker_epoll_fd_[current_worker], EPOLL_CTL_ADD, client_fd, client, EPOLLIN);
        current_worker = (current_worker + 1) % ThreadPoolSize;
    }
}

void HttpServer::process_epoll_event(int epfd, EventInfo *data, epoll_event ev) {
    int fd = data->fd;
    if (ev.events & EPOLLIN) {
        ssize_t bytes_read = read(fd, data->buffer, MaxBufferSize - 1);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                data->fd = fd;
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
            }
            else {
                close(fd);
                delete data;
                if (errno != ECONNRESET) cerr << "read error: " << strerror(errno) << endl;
            }
        }
        else if (bytes_read == 0) {
            handle_epoll_ctrl(epfd, EPOLL_CTL_DEL, fd);
            close(fd);
            delete data;
        }
        else {
            HttpRespond r = handleRequest(parseRequest(data->buffer));
            string s = r.toString();
            strncpy(data->buffer, s.c_str(), s.size());
            data->total = s.size();
            
            handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
        }
    }
    else if (ev.events & EPOLLOUT) {
        ssize_t bytes_written = write(fd, data->buffer, data->total);
        if (data->total > bytes_written) {
            data->read += bytes_written;
            data->total -= bytes_written;
            handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
        }
        else if (data->total == bytes_written) {
            EventInfo *client = new EventInfo();
            client->fd = fd;
            handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, client, EPOLLIN);
            delete data;
        }
        else {
            handle_epoll_ctrl(epfd, EPOLL_CTL_DEL, fd);
            close(fd);
            delete data;
        }
    }
    else {
        close(fd);
        delete data;
    }
}

void HttpServer::process_event(int worker_id) {
    int curr_epoll_fd = worker_epoll_fd_[worker_id];
    while (active_) {
        size_t nfds = epoll_wait(curr_epoll_fd, worker_events_[worker_id], MaxEventSize, 0);
        if (nfds < 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
            continue;
        }

        for (size_t i = 0; i < nfds; i++) {
            const epoll_event &curr_event = worker_events_[worker_id][i];
            struct EventInfo *data = reinterpret_cast<EventInfo*>(curr_event.data.ptr);
            if (curr_event.events & EPOLLIN || curr_event.events & EPOLLOUT) {
                process_epoll_event(curr_epoll_fd, data, curr_event);
            }
            else {
                handle_epoll_ctrl(curr_epoll_fd, EPOLL_CTL_DEL, data->fd);
                close(data->fd);
                delete data;
            }
        }
    }
}

void HttpServer::setup_server() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        throw std::runtime_error("socket created fail.");
    }

    memset(&server_info_, 0, sizeof(server_info_));
    server_info_.sin_family = AF_INET;
    server_info_.sin_addr.s_addr = INADDR_ANY;
    inet_pton(AF_INET, host_.c_str(), &(server_info_.sin_addr.s_addr));
    server_info_.sin_port = htons(port_);

    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        throw std::runtime_error("setsockopt fail");
    }

    if (bind(socket_fd_, (const struct sockaddr*) &server_info_, sizeof(server_info_)) < 0) {
        close(socket_fd_);
        throw std::runtime_error("bind fail");
    }

    if (listen(socket_fd_, 1024) < 0) {
        throw std::runtime_error("listen fail");
    }
}

void HttpServer::start() {
    HttpServer::setup_server();
    for (int i = 0; i < ThreadPoolSize; i++) {
        if ((worker_epoll_fd_[i] = epoll_create1(0)) < 0) {
            throw runtime_error("epoll created fail.");
        }
    }

    listen_thread_ = std::thread(&HttpServer::server_listen, this);
    for (size_t i = 0; i < ThreadPoolSize; i++) {
        worker_thread_[i] = std::thread(&HttpServer::process_event, this, i);
    }
}

void HttpServer::stop() {
    active_ = false;
    listen_thread_.join();
    for (int i = 0; i < ThreadPoolSize; i++) {
        worker_thread_[i].join();
        close(worker_epoll_fd_[i]);
    }
    close(socket_fd_);
}