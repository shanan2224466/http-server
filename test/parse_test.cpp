#include "http_parse.hpp"
#include <gtest/gtest.h>

TEST(ParseTest, header_body_test) {
    HttpRequest req;
    HttpRespond res;

    std::string raw = "GET /posts/3/users/5 HTTP/1.1\r\nHost: example.com\r\nConnection: Close\r\nContent-Length: 15\r\n\r\n{\"key\":\"value\"}";
    req = parseRequest(raw);
    EXPECT_EQ(req.method, "GET");
    EXPECT_EQ(req.path, "/posts/3/users/5");
    EXPECT_EQ(req.version, "HTTP/1.1");
    EXPECT_EQ(req.headers["Host"], "example.com");
    EXPECT_EQ(req.headers["Connection"], "close");
    EXPECT_EQ(req.headers["Content-Length"], "15");
    EXPECT_EQ(req.body, "{\"key\":\"value\"}");
}