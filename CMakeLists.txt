cmake_minimum_required(VERSION 3.20)
project(my_foxglove_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

# Library
add_library(foxglove
  src/error.cpp
  src/protocol.cpp
  src/channel.cpp
  src/server.cpp
  src/messages.cpp
  src/context.cpp
)
target_include_directories(foxglove PUBLIC include)
target_link_libraries(foxglove PUBLIC tl::expected nlohmann_json::nlohmann_json PkgConfig::LWS)

# Tests
enable_testing()
add_subdirectory(tests)

# Examples
add_subdirectory(examples/ch01_skeleton)
add_subdirectory(examples/ch02_protocol)
add_subdirectory(examples/ch03_channel)
add_subdirectory(examples/ch04_server)
add_subdirectory(examples/ch05_serialization)
add_subdirectory(examples/ch06_context)
