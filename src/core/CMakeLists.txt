# The core depends on the standard library alone; the CLI is one front end.
add_library(y8conv_core STATIC
    convert.cpp
    diag.cpp
    source.cpp
    voicedata.cpp
    writer.cpp
)
target_include_directories(y8conv_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
