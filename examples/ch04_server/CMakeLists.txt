cmake_minimum_required(VERSION 3.20)
project(ch04_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ch04_server main.cpp)
target_link_libraries(ch04_server PRIVATE foxglove)
