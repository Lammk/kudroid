# art_dex — libdexfile của AOSP ART (Android 10) đã cắt gọn cho KuDroid.
#
# Nguồn: third_party/art_src (giữ nguyên làm bản tham chiếu).
# Đã bỏ: dex_file_verifier (DEX từ APK coi là trusted), mem_map (KuDroid tự mmap),
#        dex_file_layout, tracking_registrar, external/.
# Đã thay bằng shim tự viết: android-base/*, ziparchive/*.

add_library(art_dex STATIC
    dex/compact_dex_file.cc
    dex/compact_offset_table.cc
    dex/descriptors_names.cc
    dex/dex_file.cc
    dex/dex_file_exception_helpers.cc
    dex/dex_file_loader.cc
    dex/dex_instruction.cc
    dex/modifiers.cc
    dex/primitive.cc
    dex/signature.cc
    dex/standard_dex_file.cc
    dex/type_lookup_table.cc
    dex/utf.cc

    base/enums.cc
    base/hiddenapi_flags.cc

    android-base/file.cc
    android-base/logging.cc
    android-base/stringprintf.cc
    android-base/strings.cc

    ziparchive/zip_archive.cc
)

# SYSTEM: header của AOSP có std::iterator (deprecated C++17), flexible array
# member, multi-line comment... Không sửa được vì là code upstream, và không
# muốn chúng làm nhiễu warning của code KuDroid.
target_include_directories(art_dex SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(art_dex PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(art_dex PUBLIC cxx_std_17)

# KHÔNG define ART_TARGET / ART_TARGET_ANDROID: globals.h dùng #if defined()
# nên kể cả `=0` cũng kích hoạt #error "defined for host build". KuDroid build
# như host build của ART trên cả macOS/iOS/Linux — đó là nhánh đúng.
target_compile_definitions(art_dex PUBLIC
    ART_STACK_OVERFLOW_GAP=8192
)

# Code AOSP bật -Werror với bộ cảnh báo riêng của họ; ở đây chỉ tắt những cảnh
# báo gây ồn mà không phản ánh lỗi thật khi biên dịch ngoài cây AOSP.
target_compile_options(art_dex PRIVATE
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wno-comment
    -Wno-deprecated-declarations
    -Wno-pedantic
)

find_package(ZLIB REQUIRED)
target_link_libraries(art_dex PUBLIC ZLIB::ZLIB)
