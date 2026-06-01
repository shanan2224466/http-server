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