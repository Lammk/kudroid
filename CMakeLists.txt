cmake_minimum_required(VERSION 3.20)
project(KuDroid LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ARM64 iOS cross-compile settings (when targeting iOS device/simulator)
# Usage: cmake -B build -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Configuring for iOS ARM64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
endif()

add_library(kudroid_core STATIC
    src/BionicShim.cpp
    src/elf_loader.cpp
    src/kudroid_bridge.cpp
)

target_include_directories(kudroid_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(kudroid_core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-O0 -g>
)