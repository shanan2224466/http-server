#pragma once

#include <string>
#include <regex>
#include <vector>
#include "http.hpp"

class Router {
    struct Route {
        std::string method;
        std::string url_regex;
        void (*handler)(HttpRequest* req, HttpRespond* res);
    };
    std::vector<Route> route_list; 

public:
    void register_route(std::string url, std::string m, void (*handler)(HttpRequest* req, HttpRespond* res));
    void request_route(HttpRequest* req, HttpRespond* res);
};