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

    server.use([](const HttpRequest* req, HttpRespond* res, next_func next) {
        auto start = std::chrono::steady_clock::now();
        next();
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        // std::cout << "Elapsed time: " << elapsed.count() << " ms\n";
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