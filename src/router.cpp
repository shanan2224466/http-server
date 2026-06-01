#include "router.hpp"

void Router::register_route(std::string url, std::string m, void (*handler)(HttpRequest* req, HttpRespond* res) ) {
    Route r;
    r.url_regex = url;
    r.method = m;
    r.handler = handler;
    route_list.push_back(r);
}

void Router::request_route(HttpRequest* req, HttpRespond* res) {
    for (const auto& r : route_list) {
        std::smatch match;
        std::regex re(r.url_regex);

        if (std::regex_match(req->path, match, re) && req->method.compare(r.method) == 0) {
            r.handler(req, res);
            return;
        }
    }
}