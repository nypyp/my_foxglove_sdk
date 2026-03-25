add_executable(test_error test_error.cpp)
target_link_libraries(test_error PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_error COMMAND test_error)
