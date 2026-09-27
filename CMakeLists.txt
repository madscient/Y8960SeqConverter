cmake_minimum_required(VERSION 3.18)
project(Y8960SeqConverter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(Y8CONV_BUILD_CLI   "Build the y8conv command" ON)
option(Y8CONV_BUILD_TESTS "Build the tests" ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

# The executable is handed out on its own, so it carries its C++ runtime rather
# than asking for the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/core)

if(Y8CONV_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(Y8CONV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
