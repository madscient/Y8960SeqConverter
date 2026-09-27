add_executable(convert_test convert_test.cpp)
target_link_libraries(convert_test PRIVATE y8conv_core)
add_test(NAME convert COMMAND convert_test)
