cmake_minimum_required(VERSION 3.20)
project(KuDroid LANGUAGES C CXX ASM)

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
execute_process(
    COMMAND git -C ${CMAKE_CURRENT_SOURCE_DIR} diff --quiet HEAD
    RESULT_VARIABLE KUDROID_GIT_DIRTY
    ERROR_QUIET
)
if(KUDROID_GIT_DIRTY EQUAL 1)
    set(KUDROID_GIT_HASH "${KUDROID_GIT_HASH}+dirty")
endif()

# Time of this configure (every build runs one). The compiler's __TIME__ only moves when
# the file holding the stamp is recompiled, so it cannot tell a fresh build from a stale one.
string(TIMESTAMP KUDROID_BUILD_TIME "%Y-%m-%dT%H:%M:%S")
set_source_files_properties(src/bridge/BridgeState.cpp PROPERTIES
    COMPILE_DEFINITIONS "KUDROID_BUILD_TIME=\"${KUDROID_BUILD_TIME}\"")

# The framework DEX header (382k lines) is generated, not tracked: fresh
# clones embed the pinned framework/build/framework.dex on first configure.
set(KUDROID_DEX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/kudroid/framework_dex_bytes.h")
set(KUDROID_DEX_BINARY "${CMAKE_CURRENT_SOURCE_DIR}/framework/build/framework.dex")
if(EXISTS "${KUDROID_DEX_BINARY}")
    if(NOT EXISTS "${KUDROID_DEX_HEADER}" OR "${KUDROID_DEX_BINARY}" IS_NEWER_THAN "${KUDROID_DEX_HEADER}")
        message(STATUS "Regenerating framework_dex_bytes.h from framework/build/framework.dex")
        execute_process(
            COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_framework_dex.py"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE KUDROID_DEX_EMBED_RESULT
        )
        if(NOT KUDROID_DEX_EMBED_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to generate framework_dex_bytes.h from framework.dex")
        endif()
    endif()
else()
    if(NOT EXISTS "${KUDROID_DEX_HEADER}")
        message(FATAL_ERROR
            "Missing ${KUDROID_DEX_HEADER} and ${KUDROID_DEX_BINARY}. "
            "Run: bash framework/build.sh")
    endif()
endif()

# Recompile Java changes before embedding the framework into native targets.
find_package(Java QUIET COMPONENTS Development)
find_file(KUDROID_D8_JAR NAMES r8.jar d8.jar
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/d8"
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
if(Java_FOUND AND KUDROID_D8_JAR)
    file(GLOB_RECURSE KUDROID_FRAMEWORK_JAVA CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/framework/*.java")
    file(GLOB KUDROID_FRAMEWORK_KOTLIN CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/kotlin/*.jar")
    get_filename_component(KUDROID_JAVA_BIN "${Java_JAVAC_EXECUTABLE}" DIRECTORY)
    get_filename_component(KUDROID_JAVA_HOME "${KUDROID_JAVA_BIN}" DIRECTORY)
    add_custom_command(
        OUTPUT "${KUDROID_DEX_BINARY}" "${KUDROID_DEX_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "JAVA_HOME=${KUDROID_JAVA_HOME}" "KUDROID_D8_JAR=${KUDROID_D8_JAR}"
            bash "${CMAKE_CURRENT_SOURCE_DIR}/framework/build.sh" --require-d8
        DEPENDS ${KUDROID_FRAMEWORK_JAVA} ${KUDROID_FRAMEWORK_KOTLIN}
            "${KUDROID_D8_JAR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/framework/build.sh"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_framework_dex.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Building and embedding Java framework"
        VERBATIM
    )
elseif(EXISTS "${KUDROID_DEX_BINARY}")
    add_custom_command(
        OUTPUT "${KUDROID_DEX_HEADER}"
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_framework_dex.py"
        DEPENDS "${KUDROID_DEX_BINARY}"
            "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_framework_dex.py"
        VERBATIM
    )
endif()
if(Java_FOUND AND KUDROID_D8_JAR)
    add_custom_target(kudroid_framework_dex DEPENDS "${KUDROID_DEX_BINARY}" "${KUDROID_DEX_HEADER}")
else()
    add_custom_target(kudroid_framework_dex DEPENDS "${KUDROID_DEX_HEADER}")
endif()

# ARM64 iOS cross-compile settings (when targeting iOS device/simulator)
# Usage: cmake -B build -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_ARCHITECTURES=arm64
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(STATUS "Configuring for iOS ARM64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target")
endif()

# truncated AOSP ART libdexfile (third_party/art_dex) — KuART base.
add_subdirectory(third_party/art_dex)
include(cmake/MediaCodecs.cmake)

add_library(kudroid_core STATIC
    src/APKExtractor.cpp
    src/BionicShim.cpp
    src/VFSPathRemapper.cpp
    src/platform/SystemFonts.cpp
    src/bionic_log_trampoline.S
    src/elf_loader.cpp
    src/ElfPackedRelocations.cpp
    src/ExecMemory.cpp
    src/elf_x18.cpp
    src/library_manager.cpp
    src/FlutterRuntime.cpp
    src/platform/InputShim.cpp
    src/platform/ArchiveExport.cpp
    src/platform/NativeTouchGate.cpp
    # MessageQueue's native wake slot. Separate file: it needs <condition_variable>,
    # which must not share a translation unit with jni.h's <stdio.h> (glibc 13
    # compiles bits/stdio.h against an incomplete FILE when it does).
    src/platform/NativeLooper.cpp
    src/platform/AudioShim.cpp
    src/platform/OpenSlShim.cpp
    src/platform/AudioCapture.cpp
    src/platform/CameraShim.cpp
    src/platform/NetworkState.cpp
    src/platform/ThermalShim.cpp
    src/platform/MediaShim.cpp
    src/platform/SurfaceTextureHost.cpp
    src/platform/MediaPlayerHost.cpp
    src/platform/MediaContainer.cpp
    src/platform/MediaDecoder.cpp
    src/platform/GraphicsShim.cpp
    # AChoreographer + ANativeWindow_setFrameRate. Kept out of GraphicsShim.cpp
    # because it owns a thread and a set of pipes, and nothing else in there does.
    src/platform/FramePacer.cpp
    src/abi/SyscallShim.cpp
    src/abi/Sendfile.cpp
    src/abi/SocketAbi.cpp
    src/abi/GuestVarargs.cpp
    src/abi/BlockingWaitRegistry.cpp
    src/abi/GuestSignals.cpp
    src/platform/AssetShim.cpp
    src/platform/ResourceTable.cpp
    src/platform/FrameworkResources.cpp
    src/platform/ResourceCompat.cpp
    src/platform/BundledFramework.cpp
    src/platform/FmodVorbisFallback.cpp
    src/platform/JavaCanvasRenderer.cpp
    src/platform/VectorRasterizer.cpp
    src/bridge/BridgeState.cpp
    src/bridge/BridgeShared.cpp
    src/bridge/CrashHandling.cpp
    src/bridge/ShellUI.cpp
    src/bridge/GuestLibrary.cpp
    src/bridge/AppLifecycle.cpp
    src/bridge/DiagnosticTests.cpp
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
    src/kuart/LibMemory.cpp
    src/platform/FileObserverHost.cpp
    src/kuart/LibNet.cpp
    src/kuart/LibTls.cpp
    src/kuart/LibZip.cpp
    src/kuart/JitCache.cpp
    src/kuart/JitCompiler.cpp
    # JitCache queries the platform memory policy. Keep this provider after the
    # JIT objects in the static archive so GNU ld extracts it when linking the
    # minimal KuART test targets as well as the full shell.
    src/platform/MemoryInfo.cpp
    # Same reasoning: the VFS pseudo-file writer and the syscall shims both derive
    # their CPU figures from here, so it must be extracted for every target that
    # links either of them.
    src/platform/CpuInfo.cpp
    src/kuart/OatFile.cpp
    src/kuart/VmLock.cpp
    src/framework/PermissionManager.cpp
    src/debug/Log.cpp
    src/debug/FrameWalk.cpp
    src/debug/ThreadSampler.cpp
    src/NativeCallTelemetry.cpp
)

find_package(ZLIB REQUIRED)
add_dependencies(kudroid_core kudroid_framework_dex)
target_link_libraries(kudroid_core PRIVATE ZLIB::ZLIB)
target_link_libraries(kudroid_core PUBLIC art_dex kudroid_vpx kudroid_vorbis)

# TLS for java.net: SecureTransport on Apple, OpenSSL on a Linux host (where it is exercised by
# the tests); a host with neither builds a stub that reports TLS as unavailable.
if(APPLE)
    target_sources(kudroid_core PRIVATE src/platform/TlsSessionApple.cpp)
    target_link_libraries(kudroid_core PUBLIC "-framework Security")
else()
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        target_sources(kudroid_core PRIVATE src/platform/TlsSessionOpenSsl.cpp)
        target_link_libraries(kudroid_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(kudroid_core PUBLIC KUDROID_HAS_OPENSSL=1)
    else()
        target_sources(kudroid_core PRIVATE src/platform/TlsSessionStub.cpp)
    endif()
endif()

target_compile_definitions(kudroid_core PRIVATE
    KUDROID_GIT_HASH="${KUDROID_GIT_HASH}"
)

# SQLite backs android.database.sqlite: the system library on Apple, the host's where the
# tests run. A host without it builds the stub that reports the database as unavailable.
if(APPLE)
    target_link_libraries(kudroid_core PUBLIC sqlite3)
    target_compile_definitions(kudroid_core PRIVATE KUDROID_HAVE_SQLITE3=1)
else()
    find_package(SQLite3 QUIET)
    if(SQLite3_FOUND)
        target_link_libraries(kudroid_core PUBLIC SQLite3::SQLite3)
        target_compile_definitions(kudroid_core PRIVATE KUDROID_HAVE_SQLITE3=1)
    endif()
endif()

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
target_include_directories(kudroid_core SYSTEM PRIVATE third_party/android_headers)

# kudroid_core uses Apple frameworks (AudioQueue -> AudioToolbox,
# kCFRunLoopCommonModes -> CoreFoundation, NSException/objc runtime ->
# Foundation + libobjc). Declared PUBLIC so every consumer — the iOS app AND
# host-side test executables on macOS — gets them. The iOS app previously only
# linked them itself; host tests (test_shims) pull SyscallShim.o/kudroid_bridge.o
# which reference these symbols.
if(APPLE)
    target_link_libraries(kudroid_core PUBLIC
        resolv
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework Foundation"
        # JavaCanvasRenderer (drawText) + GraphicsShim (blit canvas) used
        # CoreGraphics (CGContext*/CGColor*/CGImage*) and CoreText (CTFont*/
        #CTLine*) — if these two frameworks are missing, host test test_shims above
        # macOS arm64 fail linker "Undefined symbols _CGBitmapContextCreate..."
        "-framework CoreGraphics"
        "-framework CoreText"
        "-framework ImageIO"
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
    add_custom_target(kudroid_flutter_runtime_check
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/flutter-runtime/verify-asset.py"
            --asset "${CMAKE_CURRENT_SOURCE_DIR}/ios-app/KuDroidShell/Resources/runtime"
        VERBATIM)
    add_dependencies(kudroid_core kudroid_flutter_runtime_check)
    add_custom_target(kudroid_system_fonts_check
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-system-fonts.py"
        VERBATIM)
    add_dependencies(kudroid_core kudroid_system_fonts_check)
    
    # Define the iOS Application Target
    add_executable(KuDroidShell MACOSX_BUNDLE
        ios-app/KuDroidShell/KuDroidApp.swift
        ios-app/KuDroidShell/ContentView.swift
        ios-app/KuDroidShell/APKExport.swift
        ios-app/KuDroidShell/AppActions.swift
        ios-app/KuDroidShell/DocumentImport.swift
        icon.png
        ios-app/KuDroidShell/ReverseHistory.swift
        ios-app/KuDroidShell/RemoteDebugClient.swift
        ios-app/KuDroidShell/IOSDiagnostics.swift
        ios-app/KuDroidShell/CameraBridge.swift
        ios-app/KuDroidShell/MediaPlayerBridge.swift
        ios-app/KuDroidShell/SensorBridge.swift
        ios-app/KuDroidShell/NetworkBridge.swift
        ios-app/KuDroidShell/kudroid_bridge.h
        ios-app/KuDroidShell/THIRD_PARTY_NOTICES.txt
        ios-app/KuDroidShell/Resources/runtime/libflutter.so
        ios-app/KuDroidShell/Resources/runtime/manifest.json
        ios-app/KuDroidShell/Resources/runtime/FLUTTER_LICENSE.txt
        ios-app/KuDroidShell/Resources/runtime/DART_LICENSE.txt
        ios-app/KuDroidShell/Resources/system/etc/fonts.xml
        ios-app/KuDroidShell/Resources/system/fonts/NotoSans.ttf
        ios-app/KuDroidShell/Resources/system/NOTO_LICENSE.txt
    )

    set_source_files_properties(icon.png ios-app/KuDroidShell/THIRD_PARTY_NOTICES.txt
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_source_files_properties(
        ios-app/KuDroidShell/Resources/runtime/libflutter.so
        ios-app/KuDroidShell/Resources/runtime/manifest.json
        ios-app/KuDroidShell/Resources/runtime/FLUTTER_LICENSE.txt
        ios-app/KuDroidShell/Resources/runtime/DART_LICENSE.txt
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/runtime HEADER_FILE_ONLY TRUE)
    set_source_files_properties(ios-app/KuDroidShell/Resources/system/etc/fonts.xml
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/system/etc HEADER_FILE_ONLY TRUE)
    set_source_files_properties(ios-app/KuDroidShell/Resources/system/fonts/NotoSans.ttf
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/system/fonts HEADER_FILE_ONLY TRUE)
    set_source_files_properties(ios-app/KuDroidShell/Resources/system/NOTO_LICENSE.txt
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources/system HEADER_FILE_ONLY TRUE)
    add_custom_command(TARGET KuDroidShell POST_BUILD
        COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_standalone_launcher.py"
            "$<TARGET_BUNDLE_DIR:KuDroidShell>"
        VERBATIM)
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
        # Without it the Xcode generator compiles in the oldest Swift mode, where
        # AVAudioSession/UIApplication APIs have their pre-Swift-4 names.
        XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
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
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Network"
        "-framework Security"
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
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_elf_relocations.cpp")
        add_executable(test_elf_relocations tests/test_elf_relocations.cpp)
        target_link_libraries(test_elf_relocations PRIVATE kudroid_core)
        find_program(KUDROID_ELF_TEST_CLANG clang)
        find_program(KUDROID_ELF_TEST_LLD ld.lld)
    endif()
    if(TARGET test_elf_relocations AND KUDROID_ELF_TEST_CLANG AND KUDROID_ELF_TEST_LLD
       AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/packed_init.c")
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_packed_init.so"
            COMMAND "${KUDROID_ELF_TEST_CLANG}" --target=aarch64-linux-android24 -fPIC -nostdlib -shared -fuse-ld=lld
                -Wl,--pack-dyn-relocs=android,--hash-style=sysv,-z,max-page-size=16384
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/packed_init.c"
                -o "${CMAKE_CURRENT_BINARY_DIR}/test_packed_init.so"
            DEPENDS tests/fixtures/packed_init.c VERBATIM)
        add_custom_target(kudroid_packed_elf_fixture DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/test_packed_init.so")
        add_dependencies(test_elf_relocations kudroid_packed_elf_fixture)
        target_compile_definitions(test_elf_relocations PRIVATE
            KUDROID_PACKED_ELF_FIXTURE="${CMAKE_CURRENT_BINARY_DIR}/test_packed_init.so")
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_elf_packed.cpp")
        add_executable(test_elf_packed tests/test_elf_packed.cpp src/ElfPackedRelocations.cpp)
        target_include_directories(test_elf_packed PRIVATE include)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sendfile.cpp")
        add_executable(test_sendfile tests/test_sendfile.cpp)
        target_link_libraries(test_sendfile PRIVATE kudroid_core)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_opensl_abi.cpp")
        add_executable(test_opensl_abi tests/test_opensl_abi.cpp)
        target_include_directories(test_opensl_abi SYSTEM PRIVATE third_party/android_headers)
        target_link_libraries(test_opensl_abi PRIVATE kudroid_core)
    endif()

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shims.cpp")
        add_executable(test_shims
            tests/test_shims.cpp
        )
        target_link_libraries(test_shims PRIVATE kudroid_core)
        target_include_directories(test_shims PRIVATE include)
    endif()

    # NDK media extraction and decoding.
    add_executable(test_surface_texture tests/test_surface_texture.cpp
        src/platform/SurfaceTextureHost.cpp src/platform/MediaShim.cpp
        src/platform/MediaContainer.cpp src/platform/MediaDecoder.cpp)
    target_include_directories(test_surface_texture PRIVATE include)
    target_link_libraries(test_surface_texture PRIVATE kudroid_vpx kudroid_vorbis)
    if(UNIX AND NOT APPLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ANGLE/include/EGL/egl.h")
        add_executable(test_surface_texture_egl tests/test_surface_texture_egl.cpp
            src/platform/SurfaceTextureHost.cpp)
        target_include_directories(test_surface_texture_egl PRIVATE include third_party/ANGLE/include)
        target_link_libraries(test_surface_texture_egl PRIVATE ${CMAKE_DL_LIBS})
    endif()

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_media.cpp")
        find_package(Threads REQUIRED)
        add_executable(test_media tests/test_media.cpp)
        target_compile_definitions(test_media PRIVATE
            KUDROID_TEST_MEDIA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vp8-vorbis.webm")
        target_link_libraries(test_media PRIVATE kudroid_core Threads::Threads)
        target_include_directories(test_media PRIVATE include)
    endif()

    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_net_shims.cpp")
        add_executable(test_net_shims tests/test_net_shims.cpp)
        target_link_libraries(test_net_shims PRIVATE kudroid_core)
        target_include_directories(test_net_shims PRIVATE include)
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

    # APK extraction keeps the archive as base.apk. Loose files are not enough: a Unity
    # game reads assets/bin/Data/* out of its own ZIP via getPackageCodePath(), and with
    # no archive it starts to a black screen with no sound and no error near the cause.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_apk_extract.cpp")
        add_executable(test_apk_extract
            tests/test_apk_extract.cpp
        )
        target_link_libraries(test_apk_extract PRIVATE kudroid_core)
        target_include_directories(test_apk_extract PRIVATE include)
    endif()

    # The x18 rewrite keeps async platform clobbering unobservable; a wrong
    # rewrite corrupts every guest, so the decoder and skip rules are pinned.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_elf_x18.cpp")
        add_executable(test_elf_x18
            tests/test_elf_x18.cpp
        )
        target_link_libraries(test_elf_x18 PRIVATE kudroid_core)
        target_include_directories(test_elf_x18 PRIVATE include)
    endif()

    # A guest window must follow the CAMetalLayer the shell presents. The shell binds
    # twice (app-wide view, then the guest-app runner's), and a guest takes its window
    # once — Unity did, and kept rendering into the layer that was no longer on screen.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_metal_layer_rebind.cpp")
        add_executable(test_metal_layer_rebind
            tests/test_metal_layer_rebind.cpp
        )
        target_link_libraries(test_metal_layer_rebind PRIVATE kudroid_core)
        target_include_directories(test_metal_layer_rebind PRIVATE include)
    endif()

    # A guest thread parked forever must appear in the log. Every wait the shims
    # expose is checked THROUGH the shim, not just against the registry: a wait that
    # is trackable but not wired up would pass a registry-only test and still leave
    # the next hang invisible, which is what happened with ULTRAKILL.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_blocking_wait.cpp")
        add_executable(test_blocking_wait
            tests/test_blocking_wait.cpp
        )
        target_link_libraries(test_blocking_wait PRIVATE kudroid_core)
        target_include_directories(test_blocking_wait PRIVATE include)
    endif()

    # A guest that asks for frames must get them. ULTRAKILL brought up Vulkan, created
    # its swapchain, and then stopped forever: all six AChoreographer symbols were
    # bound to the universal dummy, so getInstance() returned NULL, Unity's pacer
    # thread read that as "no Looper on this thread", exited immediately, and never
    # signalled the condition variable its render thread was waiting on. No crash, no
    # further log output, one frame never submitted.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frame_pacer.cpp")
        add_executable(test_frame_pacer
            tests/test_frame_pacer.cpp
        )
        target_link_libraries(test_frame_pacer PRIVATE kudroid_core)
        target_include_directories(test_frame_pacer PRIVATE include)
    endif()

    add_executable(test_native_looper tests/test_native_looper.cpp)
    target_link_libraries(test_native_looper PRIVATE kudroid_core)
    target_include_directories(test_native_looper PRIVATE include)

    # Multi-finger input. The pointer table used to be a fixed array, so a batch wider
    # than it was clamped and the guest read another finger's position at the indices past
    # the clamp; a lift among fingers still down was reported as ACTION_UP, ending the
    # gesture for the finger that was staying down.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_touch_pointers.cpp")
        add_executable(test_touch_pointers
            tests/test_touch_pointers.cpp
        )
        target_link_libraries(test_touch_pointers PRIVATE kudroid_core)
        target_include_directories(test_touch_pointers PRIVATE include)
    endif()

    # What the watchdog reports while a call is wedged. Native calls nest, and the
    # record used to be one global slot that was never restored on the way out — so
    # after JNIBridge.invoke returned in 435 microseconds, 47 seconds of watchdog lines
    # carried ITS name beside nativeRender's elapsed time. The log named a method that
    # had already returned as the thing that was hung.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_call_telemetry.cpp")
        add_executable(test_call_telemetry
            tests/test_call_telemetry.cpp
        )
        target_link_libraries(test_call_telemetry PRIVATE kudroid_core)
        target_include_directories(test_call_telemetry PRIVATE include)
    endif()

    # A guest's sigaction, decoded the way the guest wrote it. `struct sigaction` has
    # three layouts in play (bionic ILP32, bionic LP64, Darwin) and they are all 32
    # bytes, so a wrong one fails no size check: KuDroid read sa_flags as the handler
    # pointer and ULTRAKILL's main thread spun at 100% of a core executing 0x18000004,
    # which is SA_SIGINFO|SA_ONSTACK|SA_RESTART. The offsets are pinned here.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_guest_signals.cpp")
        add_executable(test_guest_signals
            tests/test_guest_signals.cpp
        )
        target_link_libraries(test_guest_signals PRIVATE kudroid_core)
        target_include_directories(test_guest_signals PRIVATE include)
    endif()

    # Frame-chain walking, which must never itself be the crash. Four hand-written walks
    # existed and three validated a frame pointer with `fp > 0x1000 && fp < 0x7fffffffffff`
    # — a test that excludes null and nothing else. One of them killed a device run:
    # log_guard_acquire_diag read [fp+56] at fp=0x100000000018 and faulted at
    # 0x100000000050, on the guest's main thread, inside a diagnostic. These tests pin the
    # shared implementation using that exact address.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_frame_walk.cpp")
        add_executable(test_frame_walk
            tests/test_frame_walk.cpp
        )
        target_link_libraries(test_frame_walk PRIVATE kudroid_core)
        target_include_directories(test_frame_walk PRIVATE include)
    endif()

    # The fatal-signal breadcrumb, driven by a real signal through the real crash handler.
    # A guest handler that siglongjmps out — il2cpp's SIGABRT handler does — reaches none
    # of the crash-log writing, so for that whole class of crash this one line IS the
    # report: ULTRAKILL took SIGABRT with a guest handler installed and produced no
    # kudroid_crash.log at all. These tests pin what the line has to carry.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_crash_breadcrumb.cpp")
        add_executable(test_crash_breadcrumb
            tests/test_crash_breadcrumb.cpp
        )
        target_link_libraries(test_crash_breadcrumb PRIVATE kudroid_core)
        target_include_directories(test_crash_breadcrumb PRIVATE include)
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

    # Device CPU topology and every surface that reports it. Unity read "Cores = 0,
    # Memory = 8000mb" on a 6-core 4 GB phone: the figures were string literals and
    # hardcoded constants written independently of each other, and __sched_cpucount was
    # missing entirely so it bound to the universal dummy and returned 0. These tests pin
    # that every surface answers with the same topology.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cpu_info.cpp")
        add_executable(test_cpu_info
            tests/test_cpu_info.cpp
        )
        target_link_libraries(test_cpu_info PRIVATE kudroid_core)
        target_include_directories(test_cpu_info PRIVATE include)
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

    add_executable(test_vfs_io tests/test_vfs_io.cpp)
    target_link_libraries(test_vfs_io PRIVATE kudroid_core)
    target_include_directories(test_vfs_io PRIVATE include)

    find_package(Threads REQUIRED)
    add_executable(test_thermal tests/test_thermal.cpp)
    target_link_libraries(test_thermal PRIVATE kudroid_core Threads::Threads)
    target_include_directories(test_thermal PRIVATE include)

    # IO trace for the audio thread: FMOD's own reads/seeks must be traced (and
    # only those), in both the FILE* layer and the fd layer.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audio_trace.cpp")
        find_package(Threads REQUIRED)
        add_executable(test_audio_trace tests/test_audio_trace.cpp)
        target_link_libraries(test_audio_trace PRIVATE kudroid_core Threads::Threads)
        target_include_directories(test_audio_trace PRIVATE include)
    endif()

    find_package(Threads REQUIRED)
    if (NOT APPLE)
        add_executable(test_audio_pipeline tests/test_audio_pipeline.cpp)
        target_include_directories(test_audio_pipeline PRIVATE include)
        target_link_libraries(test_audio_pipeline PRIVATE Threads::Threads)
        add_executable(test_egl_layer_ownership tests/test_egl_layer_ownership.cpp)
        target_link_libraries(test_egl_layer_ownership PRIVATE kudroid_core)
        target_link_options(test_egl_layer_ownership PRIVATE -rdynamic)
    endif()
    add_executable(test_socket_abi tests/test_socket_abi.cpp src/abi/SocketAbi.cpp)
    target_include_directories(test_socket_abi PRIVATE include)
    add_executable(test_resolver_abi tests/test_resolver_abi.cpp)
    target_link_libraries(test_resolver_abi PRIVATE kudroid_core art_dex)
    target_include_directories(test_resolver_abi PRIVATE include)
    add_executable(test_touch_event_queue tests/test_touch_event_queue.cpp)
    target_include_directories(test_touch_event_queue PRIVATE include)
    target_link_libraries(test_touch_event_queue PRIVATE Threads::Threads)

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
    # test_kuart_jni_float     JNI float/double ABI (AAPCS64 v0-v7) + stack args
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
    # test_kuart_proxy         Proxy.newProxyInstance: the returned object must BE the
    #                          proxied interface and route its calls to the handler. The
    #                          old Java version returned a bare Proxy, so the cast failed
    #                          and the first call died with "method without body: run" —
    #                          which is where Unity's runOnUiThread stopped.
    # test_kuart_default_methods
    #                          Interface default method resolution (JVMS 5.4.3.3). An
    #                          abstract declaration in the class chain used to hide a
    #                          default, turning correct Java into AbstractMethodError, and
    #                          among several interfaces the first found won rather than the
    #                          most specific — so an overridden default ran silently.
    #                          Unity reported "default methods are only supported since
    #                          Android Oreo" on an SDK-29 runtime because of it.
    # test_kuart_method_handles
    #                          Primitive class literals and java.lang.invoke. javac compiles
    #                          int.class into a read of Integer.TYPE, and all eight TYPE
    #                          fields were null — so every reflective lookup whose signature
    #                          mentioned a primitive could never match, in any app. On top of
    #                          that, unreflectSpecial is the only way to run an interface
    #                          default method on a Proxy: an ordinary call reaches the
    #                          InvocationHandler that is asking for it and recurses.
    # test_kuart_choreographer
    #                          android.view.Choreographer, the JAVA frame API. The NDK half
    #                          (AChoreographer_*) was implemented and ULTRAKILL still stopped:
    #                          Unity uses both, and this class was a nine-line stub whose
    #                          FrameCallback interface declared no methods at all. Because the
    #                          CLASS existed, the missing methods were auto-stubbed rather than
    #                          reported, getInstance() returned null, and Unity's JNIBridge
    #                          threw NoSuchMethodError out of Looper.loop. Also pins the
    #                          auto-stub signature defect behind it: the stub left
    #                          DexMethod::signature null, which NameAndSigMatch reads as
    #                          "match anything", so getSystemService(Class) silently resolved
    #                          to getSystemService(String).
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
        test_kuart_monitor
        test_kuart_prefs
        test_kuart_jit
        test_kuart_proxy
        test_kuart_default_methods
        test_kuart_method_handles
        test_kuart_choreographer
        test_kuart_runtime
        test_kuart_resources
        test_kuart_resource_compat
        test_kuart_ime
        test_kuart_audiotrack
        test_kuart_capture
        test_kuart_camera
        test_kuart_network_bitmap
        test_kuart_openurl
        test_kuart_net
    )
        if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/${kuart_test}.cpp")
            add_executable(${kuart_test} kuart-tests/${kuart_test}.cpp)
            target_link_libraries(${kuart_test} PRIVATE kudroid_core art_dex ZLIB::ZLIB)
            target_include_directories(${kuart_test} PRIVATE include kuart-tests)
            if(kuart_test STREQUAL "test_kuart_runtime")
                target_include_directories(${kuart_test} PRIVATE src/bridge)
            endif()
        endif()
    endforeach()

    # java.net HTTP/TLS scenarios: Java test classes dexed against the framework, driven by a
    # native harness that provides the servers.
    # They compile against the framework classes and need the pinned d8, so a build that
    # cannot rebuild the framework (no third_party/d8) skips them instead of failing.
    if(Java_FOUND AND KUDROID_D8_JAR AND
       EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/test_kuart_netjava.cpp")
        set(KUART_JAVA_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/TestSupport.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/HttpTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/TlsTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/LooperTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/MediaApiTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/ThrowableTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/FrameworkResourceTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/NumberFormatTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/AxmlBuilder.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/InflaterTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/StyleTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/FrameworkApiTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/kdtest/CustomView.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/kdtest/CustomGroup.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/kdtest/NoAttrsView.java)
        set(KUART_JAVA_TEST_DEX "${CMAKE_BINARY_DIR}/kuart_java_tests.dex")
        add_custom_command(OUTPUT "${KUART_JAVA_TEST_DEX}"
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/build-java-test.sh "${KUART_JAVA_TEST_DEX}" ${KUART_JAVA_TEST_SOURCES}
            DEPENDS ${KUART_JAVA_TEST_SOURCES} kudroid_framework_dex
            COMMENT "Building java.net test classes")
        add_custom_target(kuart_java_tests_dex DEPENDS "${KUART_JAVA_TEST_DEX}")

        set(KUART_WINDOW_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/TestSupport.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/WindowTraversalTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/ThemeBridgeTests.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/BackNavigationTests.java)
        set(KUART_WINDOW_TEST_DEX "${CMAKE_BINARY_DIR}/kuart_window_tests.dex")
        add_custom_command(OUTPUT "${KUART_WINDOW_TEST_DEX}"
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/build-java-test.sh "${KUART_WINDOW_TEST_DEX}" ${KUART_WINDOW_TEST_SOURCES}
            DEPENDS ${KUART_WINDOW_TEST_SOURCES} kudroid_framework_dex
            COMMENT "Building window traversal and appearance tests")
        add_custom_target(kuart_window_tests_dex DEPENDS "${KUART_WINDOW_TEST_DEX}")
        add_executable(test_kuart_window kuart-tests/test_kuart_window.cpp)
        target_link_libraries(test_kuart_window PRIVATE kudroid_core art_dex)
        target_include_directories(test_kuart_window PRIVATE include kuart-tests)
        target_compile_definitions(test_kuart_window PRIVATE KUART_WINDOW_TEST_DEX="${KUART_WINDOW_TEST_DEX}")
        add_dependencies(test_kuart_window kuart_window_tests_dex)
        set(KUART_UI_TEST_SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/TestSupport.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/AxmlBuilder.java
            ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/java/UiAbiTests.java)
        set(KUART_UI_TEST_DEX "${CMAKE_BINARY_DIR}/kuart_ui_tests.dex")
        add_custom_command(OUTPUT "${KUART_UI_TEST_DEX}"
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/kuart-tests/build-java-test.sh "${KUART_UI_TEST_DEX}" ${KUART_UI_TEST_SOURCES}
            DEPENDS ${KUART_UI_TEST_SOURCES} kudroid_framework_dex
            COMMENT "Building UI ABI tests")
        add_custom_target(kuart_ui_tests_dex DEPENDS "${KUART_UI_TEST_DEX}")
        add_executable(test_kuart_ui kuart-tests/test_kuart_ui.cpp)
        target_link_libraries(test_kuart_ui PRIVATE kudroid_core art_dex)
        target_include_directories(test_kuart_ui PRIVATE include kuart-tests)
        target_compile_definitions(test_kuart_ui PRIVATE KUART_UI_TEST_DEX="${KUART_UI_TEST_DEX}")
        add_dependencies(test_kuart_ui kuart_ui_tests_dex)
        add_executable(test_kuart_netjava kuart-tests/test_kuart_netjava.cpp)
        target_link_libraries(test_kuart_netjava PRIVATE kudroid_core art_dex ZLIB::ZLIB)
        target_include_directories(test_kuart_netjava PRIVATE include kuart-tests)
        target_compile_definitions(test_kuart_netjava PRIVATE KUART_JAVA_TEST_DEX="${KUART_JAVA_TEST_DEX}")
        add_dependencies(test_kuart_netjava kuart_java_tests_dex)

        add_executable(test_kuart_looper kuart-tests/test_kuart_looper.cpp)
        target_link_libraries(test_kuart_looper PRIVATE kudroid_core art_dex)
        target_include_directories(test_kuart_looper PRIVATE include kuart-tests)
        target_compile_definitions(test_kuart_looper PRIVATE KUART_JAVA_TEST_DEX="${KUART_JAVA_TEST_DEX}")
        add_dependencies(test_kuart_looper kuart_java_tests_dex)

        add_executable(test_kuart_styles kuart-tests/test_kuart_styles.cpp)
        target_link_libraries(test_kuart_styles PRIVATE kudroid_core art_dex)
        target_include_directories(test_kuart_styles PRIVATE include kuart-tests)
        target_compile_definitions(test_kuart_styles PRIVATE KUART_JAVA_TEST_DEX="${KUART_JAVA_TEST_DEX}")
        add_dependencies(test_kuart_styles kuart_java_tests_dex)
    endif()

    foreach(kuart_test IN ITEMS test_kuart_runtime test_kuart_choreographer)
        if(TARGET ${kuart_test})
            set_target_properties(${kuart_test} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/kuart-tests")
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

    # Local resource-table compatibility audit for APK and raw resources.arsc inputs.
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/kuart_resource_compat.cpp")
        add_executable(kuart_resource_compat tools/kuart_resource_compat.cpp)
        target_link_libraries(kuart_resource_compat PRIVATE kudroid_core art_dex ZLIB::ZLIB)
        target_include_directories(kuart_resource_compat PRIVATE include)
    endif()
endif()
