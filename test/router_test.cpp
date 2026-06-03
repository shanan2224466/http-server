#include "router.hpp"
#include <gtest/gtest.h>

TEST(RouterTest, static_dynamic_test) {
    Router router;
    router.add("GET", "/", [](const HttpRequest* req, HttpRespond* res, auto params) {});
    router.add("GET", "/users/:user_id", [](const HttpRequest* req, HttpRespond* res, auto params) {});
    router.add("GET", "/posts/:id", [](const HttpRequest* req, HttpRespond* res, auto params) {});
    router.add("GET", "/posts/:id/users/:user_id", [](const HttpRequest* req, HttpRespond* res, auto params) {});

    Route match;
    std::unordered_map<std::string, std::string> params;
    EXPECT_TRUE(router.match("GET", "/", match, params));
    EXPECT_FALSE(router.match("POST", "/", match, params));
    EXPECT_EQ(params.size(), 0);

    params.clear();
    EXPECT_TRUE(router.match("GET", "/posts/43", match, params));
    EXPECT_EQ(params["id"], "43");

    EXPECT_FALSE(router.match("GET", "/post", match, params));

    params.clear();
    EXPECT_TRUE(router.match("GET", "/posts/3/users/5", match, params));
    EXPECT_EQ(params["id"], "3");
    EXPECT_EQ(params["user_id"], "5");

    params.clear();
    EXPECT_FALSE(router.match("GET", "/post/3/customer/5", match, params));
    EXPECT_EQ(params.size(), 0);
}

TEST(RouterTest, handler_called) {
    Router router;
    bool called = false;
    router.add("GET", "/", [&](const HttpRequest* req, HttpRespond* res, auto params) {
        called = true;
    });
    Route matched;
    std::unordered_map<std::string, std::string> params;
    router.match("GET", "/", matched, params);
    matched.handler(nullptr, nullptr, params);
    EXPECT_TRUE(called);
}

TEST(RouterTest, notfound_test) {
    Router router;
    router.add("GET", "/", [](const HttpRequest* req, HttpRespond* res, auto params) {});

    Route match;
    std::unordered_map<std::string, std::string> params;
    EXPECT_FALSE(router.match("GET", "/posts/5", match, params));
}