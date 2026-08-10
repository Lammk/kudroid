cmake_minimum_required(VERSION 3.20)
project(KuDroid LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ARM64 iOS cross-compile settings (when targeting iOS device/simulator)
# Usage: cmake -B build -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Configuring for iOS ARM64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
endif()

add_library(kudroid_core SHARED
    src/APKExtractor.cpp
    src/BionicShim.cpp
    src/VFSPathRemapper.cpp
    src/bionic_log_trampoline.S
    src/elf_loader.cpp
    src/library_manager.cpp
    src/shims/SyscallShim.cpp
    src/shims/GraphicsShim.cpp
    src/shims/InputShim.cpp
    src/kudroid_bridge.cpp
    src/kudroid_jni.cpp
    src/gpu_test.cpp
    src/DexManager.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set_target_properties(kudroid_core PROPERTIES
        FRAMEWORK TRUE
        FRAMEWORK_VERSION A
        MACOSX_FRAMEWORK_IDENTIFIER dev.kuzei13.kudroidcore
        VERSION 1.0.0
        SOVERSION 1.0.0
        INSTALL_RPATH "@executable_path/Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )

    # Link against dynamic frameworks so iOS dyld loads them automatically
    target_link_options(kudroid_core PRIVATE
        "-F${CMAKE_CURRENT_SOURCE_DIR}/third_party/ANGLE/lib/ios-arm64"
        "-F${CMAKE_CURRENT_SOURCE_DIR}/third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64"
        "-framework" "libEGL"
        "-framework" "libGLESv2"
        "-framework" "MoltenVK"
    )
endif()

add_subdirectory(third_party/jvm/miniJVM/minijvm/c)
target_link_libraries(kudroid_core PRIVATE mini_jvm_lib)

find_package(ZLIB REQUIRED)
target_link_libraries(kudroid_core PRIVATE ZLIB::ZLIB)

find_path(MINIZIP_INCLUDE_DIR minizip/unzip.h)
find_library(MINIZIP_LIBRARY NAMES minizip)
if(MINIZIP_INCLUDE_DIR AND MINIZIP_LIBRARY)
    target_compile_definitions(kudroid_core PRIVATE KUDROID_HAS_MINIZIP=1)
    target_include_directories(kudroid_core PRIVATE ${MINIZIP_INCLUDE_DIR})
    target_link_libraries(kudroid_core PRIVATE ${MINIZIP_LIBRARY})
endif()

target_include_directories(kudroid_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    third_party/jvm/miniJVM/minijvm/c/jvm
)

target_compile_options(kudroid_core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-O0 -g>
)