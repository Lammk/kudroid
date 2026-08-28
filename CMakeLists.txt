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

# Embed the git commit into the build stamp so we can tell which commit an
# IPA on a device actually contains ("no-git-hash" was making it impossible to
# tell stale builds apart). Falls back to UNKNOWN when not in a git repo.
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short HEAD
    OUTPUT_VARIABLE KUDROID_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT KUDROID_GIT_HASH)
    set(KUDROID_GIT_HASH "UNKNOWN")
endif()

# ARM64 iOS cross-compile settings (when targeting iOS device/simulator)
# Usage: cmake -B build -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Configuring for iOS ARM64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
endif()

# truncated AOSP ART libdexfile (third_party/art_dex) — KuART base.
add_subdirectory(third_party/art_dex)

add_library(kudroid_core STATIC
    src/APKExtractor.cpp
    src/BionicShim.cpp
    src/VFSPathRemapper.cpp
    src/bionic_log_trampoline.S
    src/elf_loader.cpp
    src/library_manager.cpp
    src/platform/InputShim.cpp
    src/platform/AudioShim.cpp
    src/platform/GraphicsShim.cpp
    src/abi/SyscallShim.cpp
    src/platform/AssetShim.cpp
    src/platform/JavaCanvasRenderer.cpp
    src/kudroid_bridge.cpp
    src/gpu_test.cpp
    # KuART — micro ART: load DEX directly, without going through dex2jar/JVM.
    src/KuArtRuntime.cpp
    src/kuart/DexHeap.cpp
    src/kuart/DexClass.cpp
    src/kuart/DexClassLinker.cpp
    src/kuart/DexFrame.cpp
    src/kuart/Interpreter.cpp
    src/kuart/DexJniEnv.cpp
    src/kuart/DexJniFunctions.cpp
    src/kuart/jni_trampoline.S
    src/kuart/DexReflect.cpp
    src/kuart/LibCore.cpp
    src/kuart/VmLock.cpp
    src/framework/PermissionManager.cpp
    src/debug/Log.cpp
)

find_package(ZLIB REQUIRED)
target_link_libraries(kudroid_core PRIVATE ZLIB::ZLIB)
target_link_libraries(kudroid_core PUBLIC art_dex)

target_compile_definitions(kudroid_core PRIVATE
    KUDROID_GIT_HASH="${KUDROID_GIT_HASH}"
)

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
)

# kudroid_core uses Apple frameworks (AudioQueue -> AudioToolbox,
# kCFRunLoopCommonModes -> CoreFoundation, NSException/objc runtime ->
# Foundation + libobjc). Declared PUBLIC so every consumer — the iOS app AND
# host-side test executables on macOS — gets them. The iOS app previously only
# linked them itself; host tests (test_shims) pull SyscallShim.o/kudroid_bridge.o
# which reference these symbols.
if(APPLE)
    target_link_libraries(kudroid_core PUBLIC
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework Foundation"
        # JavaCanvasRenderer (drawText) + GraphicsShim (blit canvas) used
        # CoreGraphics (CGContext*/CGColor*/CGImage*) and CoreText (CTFont*/
        #CTLine*) — if these two frameworks are missing, host test test_shims above
        # macOS arm64 fail linker "Undefined symbols _CGBitmapContextCreate..."
        "-framework CoreGraphics"
        "-framework CoreText"
        "-lobjc"
    )
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
        ios-app/KuDroidShell/RemoteDebugClient.swift
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
        # The guest's native code checks the framework's native method with
        # dlsym(dlopen(0), "Java_..."). Mach-O does not include symbols from static lib
        # dynamic symbol table so must export_dynamic. DO NOT use
        # -exported_symbol — that flag creates a whitelist, hiding all remaining symbols.
        "-Wl,-export_dynamic"
    )

    # Note: Copying of MoltenVK and ANGLE frameworks into KuDroidShell.app/Frameworks 
    # will be handled by build.yml script after xcodebuild.
else()
    # ─────────────────────────────────────────────────────────────────────────
    # Host-side tests (not built for iOS)
    # ─────────────────────────────────────────────────────────────────────────


    # Host-side regression tests for the guest syscall shims (SyscallShim.cpp).
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shims.cpp")
        add_executable(test_shims
            tests/test_shims.cpp
        )
        target_link_libraries(test_shims PRIVATE kudroid_core)
        target_include_directories(test_shims PRIVATE include)
    endif()

    # Host-side test for AndroidManifest.xml (AXML) LAUNCHER-activity parsing.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_manifest_parse.cpp")
        add_executable(test_manifest_parse
            tests/test_manifest_parse.cpp
        )
        target_link_libraries(test_manifest_parse PRIVATE kudroid_core)
        target_include_directories(test_manifest_parse PRIVATE include)
    endif()

    # Same, for manifests shipped as plain XML (apktool-repacked APKs).
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_manifest_text.cpp")
        add_executable(test_manifest_text
            tests/test_manifest_text.cpp
        )
        target_link_libraries(test_manifest_text PRIVATE kudroid_core)
        target_include_directories(test_manifest_text PRIVATE include)
    endif()

    # KuART's tests are located in kuart-tests/ (not tests/) because tests/ is broken
    # .gitignore — CI needs these files so they must be committed.
    # test_art_dex parse DEX with ART's libdexfile
    #   test_kuart          class loader: field layout + vtable
    # test_kuart_interp opcode compute, Java-vs-C++ edge
    # test_kuart_object new/field/array/invoke + dynamic dispatch
    # test_kuart_jni Java two-way JNIEnv ⇄ native
    # test_kuart_exc try/catch table + <clinit>
    #   test_kuart_reflect  Class.forName/newInstance/invoke + java.lang.Class
    # test_kuart_filled_array  filled-new-array{,/range}: enum $values(), new T[]{..}
    # test_kuart_jni_float     JNI float/double ABI (AAPCS64 v0-v7) + >6 args
    # test_kuart_stub          auto-stub policy: boot classpath only, stubs refuse use
    # test_kuart_framework     runs the real framework.dex: regex, java.text, Calendar
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/test_art_dex.cpp")
        add_executable(test_art_dex kuart-tests/test_art_dex.cpp)
        target_link_libraries(test_art_dex PRIVATE art_dex ZLIB::ZLIB)
        target_include_directories(test_art_dex PRIVATE kuart-tests)
    endif()

    foreach(kuart_test IN ITEMS
        test_kuart
        test_kuart_interp
        test_kuart_object
        test_kuart_jni
        test_kuart_exc
        test_kuart_reflect
        test_kuart_libcore
        test_kuart_filled_array
        test_kuart_jni_float
        test_kuart_stub
        test_kuart_framework
    )
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/${kuart_test}.cpp")
            add_executable(${kuart_test} kuart-tests/${kuart_test}.cpp)
            target_link_libraries(${kuart_test} PRIVATE kudroid_core art_dex ZLIB::ZLIB)
            target_include_directories(${kuart_test} PRIVATE include kuart-tests)
        endif()
    endforeach()
endif()