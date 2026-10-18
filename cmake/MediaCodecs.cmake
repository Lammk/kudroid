function(kudroid_unpack_codec name hash url)
    set(archive "${CMAKE_SOURCE_DIR}/third_party/downloads/${name}.tar.gz")
    if(NOT EXISTS "${archive}")
        file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/third_party/downloads")
        message(STATUS "Downloading ${name}.tar.gz from ${url}...")
        file(DOWNLOAD "${url}" "${archive}"
            EXPECTED_HASH SHA256=${hash}
            SHOW_PROGRESS
            TLS_VERIFY ON)
    endif()
    file(SHA256 "${archive}" actual)
    if(NOT actual STREQUAL hash)
        message(FATAL_ERROR "Invalid source archive: ${name}")
    endif()
    if(NOT EXISTS "${CMAKE_BINARY_DIR}/_deps/${name}")
        file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${CMAKE_BINARY_DIR}/_deps")
    endif()
endfunction()

kudroid_unpack_codec(libvpx-1.15.2 26fcd3db88045dee380e581862a6ef106f49b74b6396ee95c2993a260b4636aa
    "https://github.com/webmproject/libvpx/archive/refs/tags/v1.15.2.tar.gz")
kudroid_unpack_codec(libogg-1.3.6 83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638
    "https://downloads.xiph.org/releases/ogg/libogg-1.3.6.tar.gz")
kudroid_unpack_codec(libvorbis-1.3.7 0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab
    "https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz")

set(BUILD_TESTING OFF CACHE BOOL "Build dependency tests")
add_subdirectory("${CMAKE_BINARY_DIR}/_deps/libogg-1.3.6" "${CMAKE_BINARY_DIR}/_deps/ogg-build" EXCLUDE_FROM_ALL)
set(vorbis_source "${CMAKE_BINARY_DIR}/_deps/libvorbis-1.3.7")
set(vorbis_files mdct smallft block envelope window lsp lpc analysis synthesis psy info
    floor1 floor0 res0 mapping0 registry codebook sharedbook lookup bitrate)
list(TRANSFORM vorbis_files PREPEND "${vorbis_source}/lib/")
list(TRANSFORM vorbis_files APPEND ".c")
add_library(kudroid_vorbis STATIC ${vorbis_files})
target_include_directories(kudroid_vorbis PUBLIC "${vorbis_source}/include" PRIVATE "${vorbis_source}/lib")
target_link_libraries(kudroid_vorbis PUBLIC ogg)

set(vpx_source "${CMAKE_BINARY_DIR}/_deps/libvpx-1.15.2")
set(vpx_build "${CMAKE_BINARY_DIR}/_deps/libvpx-build")
file(MAKE_DIRECTORY "${vpx_build}")
set(vpx_platform generic-gnu)
set(vpx_options --disable-neon)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(vpx_platform arm64-darwin-gcc)
    set(vpx_options --enable-neon --disable-neon-asm --disable-neon-dotprod
        --disable-neon-i8mm --disable-sve --disable-sve2)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
    if(APPLE)
        set(vpx_platform arm64-darwin20-gcc)
    else()
        set(vpx_platform arm64-linux-gcc)
    endif()
    set(vpx_options --enable-neon --disable-neon-asm --disable-neon-dotprod
        --disable-neon-i8mm --disable-sve --disable-sve2)
endif()
find_program(KUDROID_MAKE NAMES gmake make REQUIRED)
find_program(KUDROID_CODEC_CC NAMES clang gcc cc)
if(NOT KUDROID_CODEC_CC)
    set(KUDROID_CODEC_CC "${CMAKE_C_COMPILER}")
endif()
find_program(KUDROID_CODEC_CXX NAMES clang++ g++ c++)
if(NOT KUDROID_CODEC_CXX)
    set(KUDROID_CODEC_CXX "${CMAKE_CXX_COMPILER}")
endif()
add_custom_command(OUTPUT "${vpx_build}/libvpx.a"
    COMMAND "${CMAKE_COMMAND}" -E env
        "CC=${KUDROID_CODEC_CC}" "CXX=${KUDROID_CODEC_CXX}" "AR=${CMAKE_AR}"
        "CFLAGS=${CMAKE_C_FLAGS} -O3 -fPIC" "CXXFLAGS=${CMAKE_CXX_FLAGS} -O3 -fPIC"
        "LDFLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        bash "${vpx_source}/configure" "--target=${vpx_platform}" --enable-external-build
        "--extra-cflags=-I${vpx_build} -I${vpx_source}"
        "--extra-cxxflags=-I${vpx_build} -I${vpx_source}"
        --disable-runtime-cpu-detect --disable-vp9 --disable-vp8-encoder --enable-vp8-decoder
        --disable-examples --disable-tools --disable-docs --disable-unit-tests
        --disable-shared --enable-static --enable-pic ${vpx_options}
    COMMAND "${KUDROID_MAKE}" -j6 CONFIG_EXTERNAL_BUILD=no libvpx.a
    WORKING_DIRECTORY "${vpx_build}"
    DEPENDS "${CMAKE_SOURCE_DIR}/third_party/downloads/libvpx-1.15.2.tar.gz"
        "${CMAKE_CURRENT_LIST_FILE}"
    VERBATIM)
add_custom_target(kudroid_vpx_build DEPENDS "${vpx_build}/libvpx.a")
add_library(kudroid_vpx STATIC IMPORTED GLOBAL)
set_target_properties(kudroid_vpx PROPERTIES IMPORTED_LOCATION "${vpx_build}/libvpx.a"
    INTERFACE_INCLUDE_DIRECTORIES "${vpx_source}")
add_dependencies(kudroid_vpx kudroid_vpx_build)
