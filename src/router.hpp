#pragma once

#include <string>
#include <regex>
#include <vector>
#include <unordered_map>
#include "http.hpp"

using route_handler = std::function<void(const HttpRequest*, HttpRespond*, std::unordered_map<std::string, std::string>&)>;

struct Route {
    std::string method;
    std::regex pattern;
    std::vector<std::string> params;
    route_handler handler;
};

class Router {
private:
    std::vector<Route> routes_;

public:
    void add(const std::string& method, const std::string& pattern, const route_handler& handler);
    bool match(const std::string& method, const std::string& url, Route &matched, std::unordered_map<std::string, std::string> &params);
};