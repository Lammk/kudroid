# Build a test .so for kudroid ELF loader validation
cmake_minimum_required(VERSION 3.20)

project(test_lib LANGUAGES C)

set(CMAKE_C_STANDARD 11)

add_library(test_lib SHARED
    test_lib.c
)

target_compile_options(test_lib PRIVATE -fPIC)

# Install to tests/ directory
set_target_properties(test_lib PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)