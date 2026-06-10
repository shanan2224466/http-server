#include "server.hpp"
#include "http_parse.hpp"
#include <algorithm>
#include <json.hpp>

using namespace std;

HttpServer::HttpServer(const string& host, const int port, ServerConfig cfg = {})
    : cfg_(cfg)
    , active_connections_(0)
    , port_(port)
    , host_(host)
    , static_dir_("")
    , socket_fd_(0)
    , server_info_()
    , active_(false)
    , router_()
    , worker_mutex_()
    , middlewares_()
    , worker_connections_()
    {
        int n = cfg_.thread_pool_size;
        worker_mutex_ = std::vector<std::mutex>(n);
        worker_connections_.assign(n, std::vector<EventInfo*>());
        worker_epoll_fd_.resize(n);
        worker_events_.assign(n, std::vector<epoll_event>(cfg_.max_connections));
        worker_thread_.resize(n);
    }

void HttpServer::use(middleware middle) {
    if (active_) throw runtime_error("call use() after start().");
    middlewares_.push_back(middle);
}

void HttpServer::execution_chain(int index, const HttpRequest* req, HttpRespond* res, const route_handler& handler, unordered_map<string, string>& params) {
    if (index == middlewares_.size()) {
        handler(req, res, params);
    }
    else {
        middlewares_[index](req, res, [&]() {
            execution_chain(index + 1, req, res, handler, params);
        });
    }
}

EventInfo* HttpServer::handle_httpdata(EventInfo* data) {
    HttpRequest req = parseRequest(data->read_buffer);
    if (req.headers["Content-Length"] != "" && stoi(req.headers["Content-Length"]) < req.body.size()) {
        return NULL;
    }
    Route matched;
    unordered_map<string, string> params;

    // honour Connection: close sent by client
    auto conn_it = req.headers.find("Connection");
    bool keep_alive = !(conn_it != req.headers.end() && conn_it->second == "close");
    data->keep_alive = keep_alive;

    HttpRespond res;
    if (router_.match(req.method, req.path, matched, params)) {
        execution_chain(0, &req, &res, matched.handler, params);
    }
    else {
        res = make_error(404, "Not Found", "Not Found");
    }
    string s = res.toString(keep_alive);
    data->write_buffer = s;
    data->write_cursor = 0;
    return data;
}

void HttpServer::add(const string& method, const string& pattern, const route_handler& call_back) {
    router_.add(method, pattern, call_back);
}

void HttpServer::close_connection(int epfd, EventInfo* data, int worker_id) {
    handle_epoll_ctrl(epfd, EPOLL_CTL_DEL, data->fd);
    close(data->fd);
    lock_guard<mutex> lock(worker_mutex_[worker_id]);
    auto& conns = worker_connections_[worker_id];
    auto it = find(conns.begin(), conns.end(), data);
    if (it != conns.end()) {
        delete *it;
        conns.erase(it);
        active_connections_--;
    }
}

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

void HttpServer::process_epoll_event(int epfd, EventInfo *data, epoll_event ev, int worker_id) {
    int fd = data->fd;
    data->last_active = chrono::steady_clock::now();
    if (ev.events & EPOLLIN) {
        char buf[4096];
        memset(buf, 0, sizeof(buf));
        ssize_t bytes_read = read(fd, buf, sizeof(buf));
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
            }
            else {
                close_connection(epfd, data, worker_id);
                if (errno != ECONNRESET) cerr << "read error: " << strerror(errno) << endl;
            }
        }
        else if (bytes_read == 0) {
            close_connection(epfd, data, worker_id);
        }
        else {
            data->read_buffer.append(buf, bytes_read);
            
            auto body = data->read_buffer.find("\r\n\r\n");
            if (body == string::npos) {
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
                return;
            }

            data = handle_httpdata(data);
            if (data == NULL) {
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
            }
            else {
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
                data->read_buffer.clear();
            }
        }
    }
    else if (ev.events & EPOLLOUT) {
        size_t remain = data->write_buffer.size() - data->write_cursor;
        ssize_t bytes_written = write(fd, data->write_buffer.c_str() + data->write_cursor, remain);
        if (bytes_written < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
            }
            else {
                close_connection(epfd, data, worker_id);
                if (errno != ECONNRESET) cerr << "write error: " << strerror(errno) << endl;
            }
        }
        else if (static_cast<size_t>(bytes_written) < remain) {
            data->write_cursor += bytes_written;
            handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
        }
        else {
            if (!data->keep_alive) {
                close_connection(epfd, data, worker_id);
            } else {
                data->write_cursor = 0;
                data->read_buffer.clear();
                data->write_buffer.clear();
                data->keep_alive = true;
                data->last_active = chrono::steady_clock::now();
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
            }
        }
    }
    else {
        close_connection(epfd, data, worker_id);
    }
}

void HttpServer::server_listen() {
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int current_worker = 0;

    while (active_) {
        int client_fd = accept(socket_fd_, (sockaddr *)&client_addr, &client_addr_len);
        if (active_connections_ > cfg_.max_connections || client_fd < 0) {
            this_thread::sleep_for(chrono::microseconds(1000));
            continue;
        }

        EventInfo *client = new EventInfo();
        client->fd = client_fd;
        lock_guard<mutex> lock(worker_mutex_[current_worker]);
        worker_connections_[current_worker].push_back(client);
        handle_epoll_ctrl(worker_epoll_fd_[current_worker], EPOLL_CTL_ADD, client_fd, client, EPOLLIN);
        current_worker = (current_worker + 1) % cfg_.thread_pool_size;
        active_connections_++;
    }
}

void HttpServer::process_event(int worker_id) {
    int curr_epoll_fd = worker_epoll_fd_[worker_id];
    while (active_) {
        int nfds = epoll_wait(curr_epoll_fd, worker_events_[worker_id].data(), cfg_.max_connections, cfg_.keepalive_timeout_s * 1000);
        if (nfds < 0) {
            this_thread::sleep_for(chrono::microseconds(1000));
            continue;
        }

        if (nfds == 0) {
            vector<EventInfo*> del;
            {
                lock_guard<mutex> lock(worker_mutex_[worker_id]);
                auto it = worker_connections_[worker_id].begin();
                while (it != worker_connections_[worker_id].end()) {
                    auto now = chrono::steady_clock::now();
                    auto idle = chrono::duration_cast<chrono::seconds>(now - (*it)->last_active).count();
                    if (idle > cfg_.keepalive_timeout_s) {
                        del.push_back(*it);
                        it = worker_connections_[worker_id].erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }
            for (auto& c : del) {
                handle_epoll_ctrl(curr_epoll_fd, EPOLL_CTL_DEL, c->fd);
                close(c->fd);
                delete c;
                active_connections_--;
            }
            continue;
        }

        for (size_t i = 0; i < nfds; i++) {
            const epoll_event &curr_event = worker_events_[worker_id][i];
            struct EventInfo *data = reinterpret_cast<EventInfo*>(curr_event.data.ptr);
            if (curr_event.events & EPOLLIN || curr_event.events & EPOLLOUT) {
                process_epoll_event(curr_epoll_fd, data, curr_event, worker_id);
            }
            else {
                close_connection(curr_epoll_fd, data, worker_id);
            }
        }
    }
}

void HttpServer::setup_server() {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        throw runtime_error("socket created fail.");
    }

    memset(&server_info_, 0, sizeof(server_info_));
    server_info_.sin_family = AF_INET;
    server_info_.sin_addr.s_addr = INADDR_ANY;
    inet_pton(AF_INET, host_.c_str(), &(server_info_.sin_addr.s_addr));
    server_info_.sin_port = htons(port_);

    int opt = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        throw runtime_error("setsockopt fail");
    }

    if (bind(socket_fd_, (const struct sockaddr*) &server_info_, sizeof(server_info_)) < 0) {
        close(socket_fd_);
        throw runtime_error("bind fail");
    }

    if (listen(socket_fd_, 1024) < 0) {
        throw runtime_error("listen fail");
    }
}

void HttpServer::start() {
    active_ = true;
    setup_server();
    for (int i = 0; i < cfg_.thread_pool_size; i++) {
        if ((worker_epoll_fd_[i] = epoll_create1(0)) < 0) {
            throw runtime_error("epoll created fail.");
        }
    }

    listen_thread_ = thread(&HttpServer::server_listen, this);
    for (size_t i = 0; i < cfg_.thread_pool_size; i++) {
        worker_thread_[i] = thread(&HttpServer::process_event, this, i);
    }
}

void HttpServer::stop() {
    active_ = false;
    listen_thread_.join();
    for (int i = 0; i < cfg_.thread_pool_size; i++) {
        worker_thread_[i].join();
        close(worker_epoll_fd_[i]);
    }
    close(socket_fd_);
}