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

add_library(kudroid_core STATIC
    src/APKExtractor.cpp
    src/BionicShim.cpp
    src/VFSPathRemapper.cpp
    src/bionic_log_trampoline.S
    src/elf_loader.cpp
    src/library_manager.cpp
    src/shims/InputShim.cpp
    src/shims/AudioShim.cpp
    src/shims/GraphicsShim.cpp
    src/shims/SyscallShim.cpp
    src/shims/AssetShim.cpp
    src/kudroid_bridge.cpp
    src/kudroid_jni.cpp
    src/kudroid_java_activity_bridge.cpp
    src/gpu_test.cpp
    src/DexManager.cpp
    src/DexCacheManager.cpp
    src/DexToJar.cpp
    src/DexAotCache.cpp
)

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
    third_party/jvm/avian/include
)

# ─────────────────────────────────────────────────────────────────────────────
# Avian JVM — built separately via its own Makefile (make platform=ios
# arch=arm64 use-clang=true). We link the resulting static library here.
# The classpath jar (Avian class library + KuDroid framework classes) is
# embedded inside libavian.a via the classpath-jar.o object (built from an
# .incbin assembly stub by scripts/patch-avian-ios.py), so no separate
# boot-jar.o is needed.
# ─────────────────────────────────────────────────────────────────────────────
set(AVIAN_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/jvm/avian/build")
set(AVIAN_STATIC_LIB "${AVIAN_BUILD_DIR}/libavian.a")

if(EXISTS "${AVIAN_STATIC_LIB}")
    target_link_libraries(kudroid_core PRIVATE "${AVIAN_STATIC_LIB}")
    message(STATUS "Linking Avian JVM: ${AVIAN_STATIC_LIB}")
else()
    message(WARNING "Avian static library not found at ${AVIAN_STATIC_LIB}. "
                    "Build it first: cd third_party/jvm/avian && make platform=ios arch=arm64 use-clang=true")
endif()

target_compile_options(kudroid_core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Debug>:-O0 -g>
)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(Swift)
    
    # Define the iOS Application Target
    add_executable(KuDroidShell MACOSX_BUNDLE
        ios-app/KuDroidShell/KuDroidApp.swift
        ios-app/KuDroidShell/ContentView.swift
        ios-app/KuDroidShell/kudroid_bridge.h
    )

    set_target_properties(KuDroidShell PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios-app/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "KuDroid"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.kudroid.shell"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.kudroid.shell"
        XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
        XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/ios-app/KuDroidShell/kudroid_bridge.h"
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
        XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ANGLE/lib/ios-arm64 ${CMAKE_CURRENT_SOURCE_DIR}/third_party/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64"
    )

    # Link standard frameworks and kudroid_core
    target_link_libraries(KuDroidShell PRIVATE
        kudroid_core
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
        "-framework UIKit"
        "-framework CoreGraphics"
        "-framework IOSurface"
        # Avian khởi tạo boot classpath qua `-Xbootclasspath:[classpathJar]`:
        # BuiltinElement gọi dlsym(main_executable, "classpathJar"). Symbol này do
        # boot.o (classpathJar()) + classpath-jar.o (jar nhúng) cung cấp và được
        # kéo vào app nhờ tham chiếu từ kudroid_jni.cpp — nhưng Mach-O không export
        # symbol từ static lib vào main executable mặc định. Flag này đưa classpathJar
        # vào dynamic symbol table để dlsym tìm thấy (giống Avian dùng -Wl,-export_dynamic
        # cho executable của chính nó).
        "-Wl,-exported_symbol,_classpathJar"
    )

    # Note: Copying of MoltenVK and ANGLE frameworks into KuDroidShell.app/Frameworks 
    # will be handled by build.yml script after xcodebuild.
else()
    # ─────────────────────────────────────────────────────────────────────────
    # Host-side tests (not built for iOS)
    # ─────────────────────────────────────────────────────────────────────────
    add_executable(test_dex_to_jar
        tests/test_dex_to_jar.cpp
    )
    target_link_libraries(test_dex_to_jar PRIVATE kudroid_core)
    target_include_directories(test_dex_to_jar PRIVATE include)

    # Host-side regression tests for the guest syscall shims (SyscallShim.cpp).
    add_executable(test_shims
        tests/test_shims.cpp
    )
    target_link_libraries(test_shims PRIVATE kudroid_core)
    target_include_directories(test_shims PRIVATE include)
endif()