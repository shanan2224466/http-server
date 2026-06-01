#include "router.hpp"

void Router::add(const std::string& method, const std::string& pattern, const route_handler& call_back) {
    Route r;
    r.method = method;

    std::string reg_pattern = "";
    std::string param = "";
    for (int i = 0; i < pattern.size(); i++) {
        if (pattern[i] == ':') {
            while (++i < pattern.size() && pattern[i] != '/') {
                param += pattern[i];
            }
            r.params.push_back(param);
            reg_pattern += "([^/]+)";
            param = "";
            --i;
        }
        else {
            reg_pattern += pattern[i];
        }
    }
    r.pattern = reg_pattern;
    r.handler = call_back;
    routes_.push_back(r);
}

bool Router::match(const std::string& method, const std::string& url, Route &matched, std::unordered_map<std::string, std::string> &params_list) {
    for (const auto& route : routes_) {
        std::smatch match;

        if (std::regex_match(url, match, route.pattern) && method.compare(route.method) == 0) {
            for (int i = 0; i < route.params.size(); i++) {
                params_list[route.params[i]] = match[i + 1];
            }
            matched = route;
            return true;
        }
    }
    return false;
}