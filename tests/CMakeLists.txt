add_executable(test_error test_error.cpp)
target_link_libraries(test_error PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_error COMMAND test_error)

add_executable(test_protocol test_protocol.cpp)
target_link_libraries(test_protocol PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_protocol COMMAND test_protocol)

add_executable(test_channel test_channel.cpp)
target_link_libraries(test_channel PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_channel COMMAND test_channel)

add_executable(test_server test_server.cpp)
target_link_libraries(test_server PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_server COMMAND test_server)

add_executable(test_messages test_messages.cpp)
target_link_libraries(test_messages PRIVATE foxglove Catch2::Catch2WithMain)
target_compile_definitions(test_messages PRIVATE FOXGLOVE_TEST_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_test(NAME test_messages COMMAND test_messages)

add_executable(test_context test_context.cpp)
target_link_libraries(test_context PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_context COMMAND test_context)

add_executable(test_mcap test_mcap.cpp)
target_link_libraries(test_mcap PRIVATE foxglove Catch2::Catch2WithMain)
add_test(NAME test_mcap COMMAND test_mcap)
