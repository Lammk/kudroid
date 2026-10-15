# cmake/ios-linux-toolchain.cmake
# Cross-compilation toolchain for arm64-apple-ios using Clang on Linux.

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)

set(IPHONEOS_SDK "$ENV{HOME}/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk")
if(NOT EXISTS "${IPHONEOS_SDK}")
    file(GLOB IPHONEOS_SDK_LIST "$ENV{HOME}/.swiftpm/swift-sdks/darwin.artifactbundle/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS*.sdk")
    list(GET IPHONEOS_SDK_LIST 0 IPHONEOS_SDK)
endif()

set(CMAKE_OSX_SYSROOT "${IPHONEOS_SDK}" CACHE PATH "Sysroot for iOS" FORCE)
set(CMAKE_SYSROOT "${IPHONEOS_SDK}")
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architecture" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target" FORCE)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

set(CMAKE_C_COMPILER_TARGET arm64-apple-ios15.0)
set(CMAKE_CXX_COMPILER_TARGET arm64-apple-ios15.0)
set(CMAKE_ASM_COMPILER_TARGET arm64-apple-ios15.0)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_INIT "-arch arm64 -isysroot ${IPHONEOS_SDK} -target arm64-apple-ios15.0")
set(CMAKE_CXX_FLAGS_INIT "-arch arm64 -isysroot ${IPHONEOS_SDK} -target arm64-apple-ios15.0")
set(CMAKE_ASM_FLAGS_INIT "-arch arm64 -isysroot ${IPHONEOS_SDK} -target arm64-apple-ios15.0")

set(CMAKE_AR llvm-ar CACHE FILEPATH "Archiver" FORCE)
set(CMAKE_RANLIB llvm-ranlib CACHE FILEPATH "Ranlib" FORCE)
