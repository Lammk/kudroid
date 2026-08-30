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
    src/abi/GuestVarargs.cpp
    src/platform/AssetShim.cpp
    src/platform/BundledFramework.cpp
    src/platform/JavaCanvasRenderer.cpp
    src/platform/MemoryInfo.cpp
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
    src/kuart/JitCache.cpp
    src/kuart/JitCompiler.cpp
    src/kuart/OatFile.cpp
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

    # Device memory figures: apps size caches from them, so they must be real,
    # self-consistent and never zero.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_memory_info.cpp")
        add_executable(test_memory_info
            tests/test_memory_info.cpp
        )
        target_link_libraries(test_memory_info PRIVATE kudroid_core)
        target_include_directories(test_memory_info PRIVATE include)
    endif()

    # The guest filesystem view: containment (no path may escape android_root) and that
    # initialize() is not redone on every file operation.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_vfs.cpp")
        add_executable(test_vfs
            tests/test_vfs.cpp
        )
        target_link_libraries(test_vfs PRIVATE kudroid_core)
        target_include_directories(test_vfs PRIVATE include)
    endif()

    # AAssetManager path resolution. An APK may nest a directory named assets/ inside its
    # assets, and an AAssetManager path is relative to the outer one — so the nested case
    # needs the literal path, not a rewritten one.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_assets.cpp")
        add_executable(test_assets
            tests/test_assets.cpp
        )
        target_link_libraries(test_assets PRIVATE kudroid_core)
        target_include_directories(test_assets PRIVATE include)
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
    # test_kuart_missing_field an unresolvable field must name itself, not say "iput"
    # test_kuart_thread_state  siglongjmp out of the interpreter: no stale frames, no
    #                          held VM lock, no leaked depth; untrusted receiver classes
    # test_kuart_vmlock        a blocking native method must not hold the VM lock, or the
    #                          Java thread that would release it cannot run — Minecraft
    #                          stopped (not crashed) exactly there, waiting on a flag no
    #                          other thread could set
    # test_kuart_prefs         Java file I/O and SharedPreferences persistence. Both had
    #                          silent failures: FileOutputStream.write was an empty method,
    #                          and Java paths skipped the VFS remapper so every write aimed
    #                          at the real /data/data, which iOS forbids
    # test_kuart_jit           the arm64 encoder against GNU as byte for byte, the
    #                          executable-memory cache (including the no-JIT path that
    #                          stock iOS and LiveContainer JITLess take), and the OAT
    #                          profile's round trip and staleness checks.
    #                          scripts/test-jit-arm64.sh runs the generated code.
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
        test_kuart_missing_field
        test_kuart_thread_state
        test_kuart_vmlock
        test_kuart_prefs
        test_kuart_jit
    )
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/${kuart_test}.cpp")
            add_executable(${kuart_test} kuart-tests/${kuart_test}.cpp)
            target_link_libraries(${kuart_test} PRIVATE kudroid_core art_dex ZLIB::ZLIB)
            target_include_directories(${kuart_test} PRIVATE include kuart-tests)
        endif()
    endforeach()

    # kuart_verify — what a guest APK references that KuART cannot resolve.
    #
    # A developer tool, not a test: it takes extracted APK directories and reports the
    # boot-classpath classes, methods and fields they need and KuART lacks, ranked across a
    # corpus by how many apps need each one. That replaces finding missing API one device
    # round-trip at a time, which is how it was being found.
    #
    # Links kudroid_core so it asks the real DexClassLinker. A standalone DEX parser would
    # drift from the runtime, and the question is what KuART fails to resolve — not what a
    # model of KuART fails to resolve.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/kuart_verify.cpp")
        add_executable(kuart_verify tools/kuart_verify.cpp)
        target_link_libraries(kuart_verify PRIVATE kudroid_core art_dex ZLIB::ZLIB)
        target_include_directories(kuart_verify PRIVATE include third_party/art_dex)
    endif()
endif()