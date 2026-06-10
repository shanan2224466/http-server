#pragma once
#include <map>
#include <string>

constexpr static int KeepAliveTimeout = 5;

class HttpRespond {
public:
    int status_code;
    std::string status_text;
    std::string content_type;
    std::string body;

    std::string toString(bool keep_alive = true) const {
        std::string response = "HTTP/1.1 " + std::to_string(status_code) + " " + status_text + "\r\n";
        response += "Content-Type: " + content_type + "\r\n";
        response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        if (keep_alive) {
            response += "Connection: keep-alive\r\n";
            response += "Keep-Alive: timeout=" + std::to_string(KeepAliveTimeout) + "\r\n";
        } else {
            response += "Connection: close\r\n";
        }
        response += "\r\n";
        response += body;
        return response;
    }
};

inline HttpRespond make_error(int code, const std::string& text, const std::string& msg) {
    HttpRespond r;
    r.status_code = code;
    r.status_text = text;
    r.content_type = "application/json";
    r.body = "{\"error\":\"" + msg + "\"}";
    return r;
}

class HttpRequest {
public:
    std::string method;
    std::string path;
    std::string version;
    std::string body;
    std::map<std::string, std::string> headers;
};