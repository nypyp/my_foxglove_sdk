cmake_minimum_required(VERSION 3.20)
project(my_foxglove_sdk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/dependencies.cmake)

# Library
add_library(foxglove
  src/error.cpp
)
target_include_directories(foxglove PUBLIC include)
target_link_libraries(foxglove PUBLIC tl::expected)

# Tests
enable_testing()
add_subdirectory(tests)

# Examples
add_subdirectory(examples/ch01_skeleton)
