include(FetchContent)

# Find libwebsockets via pkg-config (system package)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LWS REQUIRED IMPORTED_TARGET libwebsockets)

FetchContent_Declare(
  tl-expected
  GIT_REPOSITORY https://github.com/TartanLlama/expected.git
  GIT_TAG        v1.1.0
)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.2
)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)

# Disable tl-expected tests to avoid Catch2 version conflict
set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tl-expected Catch2 nlohmann_json)
