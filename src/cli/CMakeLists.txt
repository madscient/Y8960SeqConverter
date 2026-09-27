add_executable(y8conv main.cpp)
target_link_libraries(y8conv PRIVATE y8conv_core)

# The manifest sets the process code page to UTF-8. The linker merges it into
# the one it generates.
if(WIN32)
    target_sources(y8conv PRIVATE y8conv.manifest)
endif()

# Handed out on its own, so the C++ runtime goes inside rather than depending on
# the libstdc++ of whatever distribution runs it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT APPLE)
    target_link_options(y8conv PRIVATE -static-libstdc++ -static-libgcc)
endif()
