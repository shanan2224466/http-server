#include <string>
#include "server.hpp"

int main() {
    std::string host = "0.0.0.0";
    int port = 8080;
    HttpServer server(host, port);
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