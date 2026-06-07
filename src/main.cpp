#include <string>
#include "server.hpp"

int main() {
    std::string host = "0.0.0.0";
    int port = 8080;
    HttpServer server(host, port);
    server.add("GET", "/", [](const HttpRequest* req, HttpRespond* res, auto params) {
        res->status_code = 200;
        res->status_text = "OK";
        res->body = "{\"message\": \"Welcome\"}";
    });
    server.add("GET", "/posts/:id", [](const HttpRequest* req, HttpRespond* res, auto params) {
        res->status_code = 200;
        res->status_text = "OK";
        res->body = "{\"id\": \"" + params["id"] + "\"}";
    });
    server.add("POST", "/posts", [](const HttpRequest* req, HttpRespond* res, auto params) {
        res->status_code = 201;
        res->status_text = "Created";
        res->body = req->body;
    });
    server.add("GET", "/static/.*", [&server](const HttpRequest* req, HttpRespond* res, auto params) {
        server.serve_file(req, res);
    });

    server.set_static_dir("/app/static");

    server.use([](const HttpRequest* req, HttpRespond* res, next_func next) {
        auto start = std::chrono::steady_clock::now();
        next();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        std::cout << req->method << " " << req->path << " " << elapsed.count() << "ms\n";
    });

    server.start();

    std::string input;
    do {
        std::cout << "% ";
        std::getline(std::cin, input);
        if (input == "quit" || input == "exit") break;
    } while (true);
    server.stop();
    return 0;
}