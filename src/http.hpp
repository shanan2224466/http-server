#pragma once
#include <string>

class HttpRespond {
public:
    int status_code;
    std::string status_text;
    std::string body;

    std::string toString() const {
        std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
        response += "Content-Type: application/json\r\n";
        response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        response += "\r\n";
        response += body;
        return response;
    }
};

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
};