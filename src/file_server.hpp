#pragma once
#include "http.hpp"

bool check_path_exist(const std::string& path);
bool check_path_legal(const std::string& path, const std::string& static_dir);
void serve_file(const HttpRequest* req, HttpRespond* res, const std::string& static_dir);
std::string get_content_type(const std::string &path);