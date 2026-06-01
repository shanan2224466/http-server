cmake_minimum_required(VERSION 3.10)
project(http-server)

set(BINARY server)
set(CMAKE_CXX_STANDARD 17)
include_directories(
    src
    include
    )
file(GLOB sources src/*.cpp src/*.hpp)
add_executable(${BINARY} ${sources})