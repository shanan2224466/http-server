cmake_minimum_required(VERSION 3.10)
project(http-server)

set(BINARY server)
set(CMAKE_CXX_STANDARD 17)
include_directories(
    src
    nlohmann
)
add_executable(server
    src/main.cpp
    src/server.cpp
    src/router.cpp
    src/http_parse.cpp
    src/file_server.cpp
)

find_package(GTest REQUIRED)
enable_testing()

add_executable(tests
    test/router_test.cpp
    test/parse_test.cpp
    src/router.cpp
    src/http_parse.cpp
)

target_link_libraries(tests GTest::gtest_main)
add_test(NAME tests COMMAND tests)
include(GoogleTest)
gtest_discover_tests(tests)