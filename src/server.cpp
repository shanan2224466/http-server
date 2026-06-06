#include "server.hpp"

using namespace std;

HttpRequest parseRequest(const string& raw) {
    HttpRequest r;
    stringstream ss(raw);
    ss >> r.method >> r.path >> r.version;
    // parse headers: each "Key: Value\r\n" line until the blank line
    string line;
    getline(ss, line);
    while (getline(ss, line)) {
        if (line == "\r" || line.empty()) break;
        auto colon = line.find(':');
        if (colon == string::npos) continue;
        string key   = line.substr(0, colon);
        string value = line.substr(colon + 1);
        // trim leading spaces and trailing \r
        auto start = value.find_first_not_of(' ');
        if (start != string::npos) value = value.substr(start);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        r.headers[key] = value;
    }
    if (r.headers["Content-Type"] == "application/json") {
        r.body = ss.str().substr(ss.tellg());
        auto j = nlohmann::json::parse(r.body);
    }
    return r;
}

HttpServer::HttpServer(const string& host, const int port)
    : port_(port)
    , host_(host)
    , static_dir_("")
    , socket_fd_(0)
    , server_info_()
    , active_(false)
    , router_()
    , worker_mutex()
    , middlewares_()
    , worker_connections_()
    {}

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

bool HttpServer::check_path_exist(const string& path) {
    try {
        filesystem::path resolved_path = filesystem::canonical(path);
        return filesystem::exists(resolved_path);
    }
    catch (const filesystem::filesystem_error& e) {
        cerr << "filesystem error: " << e.what() << endl;
        return false;
    }
}

bool HttpServer::check_path_legal(const string& path) {
    try {
        filesystem::path resolved_path = filesystem::canonical(path);
        filesystem::path resolved_base = filesystem::canonical(static_dir_);

        auto base_it = resolved_base.begin();
        auto user_it = resolved_path.begin();
        for (; base_it != resolved_base.end(); ++base_it, ++user_it) {
            if (user_it == resolved_path.end() || *base_it != *user_it) {
                return false; 
            }
        }
        return true;
    }
    catch (const filesystem::filesystem_error& e) {
        cerr << "filesystem error: " << e.what() << endl;
        return false;
    }
}

string HttpServer::get_content_type(const string &path) {
    const size_t pos = path.find_last_of(".");
    if (pos != string::npos) {
        const string extension = path.substr(pos + 1);
        if(extension == "html" || extension == "htm" || extension == "shtml")
            return "text/html";
        if (extension == "json")
            return "application/json";
        if (extension == "js")
            return "application/javascript";
        if (extension == "css")
            return "text/css";
        if (extension == "jpg" || extension == "jpg")
            return "image/jpeg";
        if (extension == "gif")
            return "image/gif";
        if (extension == "ico")
            return "image/x-icon";
        if (extension == "png")
            return "image/png";
        if (extension == "svg" || extension == "svgz")
            return "image/svg+xml";
    }
    return "application/octet-stream";
}

void HttpServer::serve_file(const HttpRequest* req, HttpRespond* res) {
    auto pos = req->path.find("/static");
    string full_path = static_dir_ + req->path.substr(pos + 7);

    if (!check_path_legal(full_path)) {
        res->status_code = 403;
        res->status_text = "Forbidden";
        res->body = "{\"error\":\"Forbidden\"}";
    }
    else if (!check_path_exist(full_path)) {
        res->status_code = 404;
        res->status_text = "Not Found";
        res->body = "{\"error\":\"Not Found\"}";
    }
    else if (access(full_path.c_str(), R_OK) != 0) {
        res->status_code = 403;
        res->status_text = "Forbidden";
        res->body = "{\"error\":\"Forbidden\"}";
    }
    else {
        ifstream file(full_path, ios::binary);
        if (!file.is_open()) {
            res->status_code = 403;
            res->status_text = "Forbidden";
            res->body = "{\"error\":\"Forbidden\"}";
        }
        res->content_type = get_content_type(req->path);

        res->body = string(
            istreambuf_iterator<char>(file),
            istreambuf_iterator<char>()
        );
        res->status_code = 200;
        res->status_text = "OK";
    }
}

EventInfo* HttpServer::handle_httpdata(EventInfo* data) {
    HttpRequest req = parseRequest(data->read_buffer);
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
        res.status_code = 404;
        res.status_text = "Not Found";
        res.body = "{\"error\":\"Not Found\"}";
    }
    string s = res.toString(keep_alive);
    data->write_buffer = s;
    data->write_cursor = 0;
    return data;
}

void HttpServer::add(const string& method, const string& pattern, const route_handler& call_back) {
    router_.add(method, pattern, call_back);
}

void HttpServer::delete_fd(int fd, int worker_id) {
    worker_mutex[worker_id].lock();
    auto it = worker_connections_[worker_id].begin();
    while (it != worker_connections_[worker_id].end()) {
        if ((*it)->fd == fd) {
            it = worker_connections_[worker_id].erase(it);
        }
        else {
            ++it;
        }
    }
    worker_mutex[worker_id].unlock();
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
                close(fd);
                delete_fd(fd, worker_id);
                delete data;
                if (errno != ECONNRESET) cerr << "read error: " << strerror(errno) << endl;
            }
        }
        else if (bytes_read == 0) {
            handle_epoll_ctrl(epfd, EPOLL_CTL_DEL, fd);
            close(fd);
            delete_fd(fd, worker_id);
            delete data;
        }
        else {
            data->read_buffer.append(buf, bytes_read);
            data->read_cursor += bytes_read;
            
            auto body = data->read_buffer.find("\r\n\r\n");
            if (body == string::npos) {
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
                return;
            }

            auto cont = data->read_buffer.find("Content-Length: ");
            if (cont != string::npos) {
                int content_length = stoi(data->read_buffer.substr(cont + 16));
                if (data->read_buffer.size() - body - 4 < content_length) {
                    handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
                    return;
                }
            }
            data = handle_httpdata(data);
            handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
            data->read_buffer.clear();
            data->read_cursor = 0;
        }
    }
    else if (ev.events & EPOLLOUT) {
        size_t remain = data->write_buffer.size() - data->write_cursor;
        ssize_t bytes_written = write(fd, data->write_buffer.c_str() + data->write_cursor, remain);
        if (remain > bytes_written) {
            data->write_cursor += bytes_written;
            handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLOUT);
        }
        else if (remain == bytes_written) {
            if (!data->keep_alive) {
                handle_epoll_ctrl(epfd, EPOLL_CTL_DEL, fd);
                close(fd);
                delete_fd(fd, worker_id);
                delete data;
            } else {
                data->read_cursor = 0;
                data->write_cursor = 0;
                data->read_buffer.clear();
                data->write_buffer.clear();
                data->keep_alive = true;
                data->last_active = chrono::steady_clock::now();
                handle_epoll_ctrl(epfd, EPOLL_CTL_MOD, fd, data, EPOLLIN);
            }
        }
        else {
            handle_epoll_ctrl(epfd, EPOLL_CTL_DEL, fd);
            close(fd);
            delete_fd(fd, worker_id);
            delete data;
        }
    }
    else {
        close(fd);
        delete_fd(fd, worker_id);
        delete data;
    }
}

void HttpServer::server_listen() {
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int current_worker = 0;

    while (active_) {
        int client_fd = accept(socket_fd_, (sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            this_thread::sleep_for(chrono::microseconds(1000));
            continue;
        }

        EventInfo *client = new EventInfo();
        client->fd = client_fd;
        worker_mutex[current_worker].lock();
        worker_connections_[current_worker].push_back(client);
        worker_mutex[current_worker].unlock();
        handle_epoll_ctrl(worker_epoll_fd_[current_worker], EPOLL_CTL_ADD, client_fd, client, EPOLLIN);
        current_worker = (current_worker + 1) % ThreadPoolSize;
    }
}

void HttpServer::process_event(int worker_id) {
    int curr_epoll_fd = worker_epoll_fd_[worker_id];
    while (active_) {
        int nfds = epoll_wait(curr_epoll_fd, worker_events_[worker_id], MaxEventSize, KeepAliveTimeout * 1000);
        if (nfds < 0) {
            this_thread::sleep_for(chrono::microseconds(1000));
            continue;
        }

        if (nfds == 0) {
            worker_mutex[worker_id].lock();
            auto it = worker_connections_[worker_id].begin();
            while (it != worker_connections_[worker_id].end()) {
                auto now = chrono::steady_clock::now();
                auto idle = chrono::duration_cast<chrono::seconds>(now - (*it)->last_active).count();
                EventInfo *temp = *it;
                if (idle > KeepAliveTimeout) {
                    handle_epoll_ctrl(curr_epoll_fd, EPOLL_CTL_DEL, (*it)->fd);
                    close((*it)->fd);
                    it = worker_connections_[worker_id].erase(it);
                    delete temp;
                }
                else {
                    ++it;
                }
            }
            worker_mutex[worker_id].unlock();
        }

        for (size_t i = 0; i < nfds; i++) {
            const epoll_event &curr_event = worker_events_[worker_id][i];
            struct EventInfo *data = reinterpret_cast<EventInfo*>(curr_event.data.ptr);
            if (curr_event.events & EPOLLIN || curr_event.events & EPOLLOUT) {
                process_epoll_event(curr_epoll_fd, data, curr_event, worker_id);
            }
            else {
                handle_epoll_ctrl(curr_epoll_fd, EPOLL_CTL_DEL, data->fd);
                close(data->fd);
                delete_fd(data->fd, worker_id);
                delete data;
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
    HttpServer::setup_server();
    for (int i = 0; i < ThreadPoolSize; i++) {
        if ((worker_epoll_fd_[i] = epoll_create1(0)) < 0) {
            throw runtime_error("epoll created fail.");
        }
    }

    listen_thread_ = thread(&HttpServer::server_listen, this);
    for (size_t i = 0; i < ThreadPoolSize; i++) {
        worker_thread_[i] = thread(&HttpServer::process_event, this, i);
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