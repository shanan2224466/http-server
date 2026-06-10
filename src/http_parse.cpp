#include "http_parse.hpp"
#include "server.hpp"
#include <string>
#include <sstream>

using namespace std;
HttpRequest parseRequest(const string& raw) {
    HttpRequest r;
    stringstream ss(raw);
    ss >> r.method >> r.path >> r.version;
    
    string line;
    getline(ss, line);
    while (getline(ss, line)) {
        if (line == "\r" || line.empty()) break;
        auto colon = line.find(':');
        if (colon == string::npos) continue;
        string key   = line.substr(0, colon);
        string value = line.substr(colon + 1);
        
        auto start = value.find_first_not_of(' ');
        if (start != string::npos) value = value.substr(start);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        
        transform(value.begin(), value.end(), value.begin(), ::tolower);
        r.headers[key] = value;
    }
    if (r.headers.count("Content-Length")) {
        r.body = ss.str().substr(ss.tellg());
    }
    return r;
}