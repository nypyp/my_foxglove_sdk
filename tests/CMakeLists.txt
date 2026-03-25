add_executable(test_error test_error.cpp)
target_link_libraries(test_error PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_error COMMAND test_error)

add_executable(test_protocol test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_protocol COMMAND test_protocol)

add_executable(test_channel test_channel.cpp)
target_link_libraries(test_channel PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_channel COMMAND test_channel)
