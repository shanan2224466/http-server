#include "file_server.hpp"
#include "server.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unistd.h>
using namespace std;


bool check_path_exist(const string& path) {
    try {
        filesystem::path resolved_path = filesystem::canonical(path);
        return filesystem::exists(resolved_path);
    }
    catch (const filesystem::filesystem_error& e) {
        cerr << "filesystem error: " << e.what() << endl;
        return false;
    }
}

bool check_path_legal(const string& path, const string& static_dir) {
    try {
        filesystem::path resolved_path = filesystem::canonical(path);
        filesystem::path resolved_base = filesystem::canonical(static_dir);

        auto base_it = resolved_base.begin();
        auto user_it = resolved_path.begin();
        for (; base_it != resolved_base.end(); ++base_it, ++user_it) {
            if (user_it == resolved_path.end() || *base_it != *user_it) {
                return false;
            }
        }
        return true;
    }
    catch (const filesystem::filesystem_error& e) {
        cerr << "filesystem error: " << e.what() << endl;
        return false;
    }
}

string get_content_type(const string &path) {
    const size_t pos = path.find_last_of(".");
    if (pos != string::npos) {
        const string extension = path.substr(pos + 1);
        if(extension == "html" || extension == "htm" || extension == "shtml")
            return "text/html";
        if (extension == "json")
            return "application/json";
        if (extension == "js")
            return "application/javascript";
        if (extension == "css")
            return "text/css";
        if (extension == "jpg" || extension == "jpeg")
            return "image/jpeg";
        if (extension == "gif")
            return "image/gif";
        if (extension == "ico")
            return "image/x-icon";
        if (extension == "png")
            return "image/png";
        if (extension == "svg" || extension == "svgz")
            return "image/svg+xml";
    }
    return "application/octet-stream";
}

void serve_file(const HttpRequest* req, HttpRespond* res, const string& static_dir) {
    auto pos = req->path.find("/static");
    string full_path = static_dir + req->path.substr(pos + 7);

    if (!check_path_legal(full_path, static_dir)) {
        *res = make_error(403, "Forbidden", "Forbidden");
    }
    else if (!check_path_exist(full_path)) {
        *res = make_error(404, "Not Found", "Not Found");
    }
    else if (access(full_path.c_str(), R_OK) != 0) {
        *res = make_error(403, "Forbidden", "Forbidden");
    }
    else {
        ifstream file(full_path, ios::binary);
        if (!file.is_open()) {
            *res = make_error(403, "Forbidden", "Forbidden");
        } else {
            res->content_type = get_content_type(req->path);
            res->body = string(
                istreambuf_iterator<char>(file),
                istreambuf_iterator<char>()
            );
            res->status_code = 200;
            res->status_text = "OK";
        }
    }
}