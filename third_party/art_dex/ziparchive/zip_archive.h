// Shim thay libziparchive của AOSP — chỉ đủ cho dex_file_loader.cc: mở ZIP từ
// bộ nhớ, tìm entry theo tên, giải nén vào buffer. Đọc central directory trực
// tiếp, inflate bằng zlib (KuDroid đã link zlib cho APKExtractor).
#ifndef KUDROID_ZIPARCHIVE_ZIP_ARCHIVE_H_
#define KUDROID_ZIPARCHIVE_ZIP_ARCHIVE_H_

#include <stddef.h>
#include <stdint.h>

#include <string>

struct ZipEntry {
    uint16_t method = 0;
    uint32_t crc32 = 0;
    uint32_t compressed_length = 0;
    uint32_t uncompressed_length = 0;
    uint32_t offset = 0;  // offset của local file header trong archive
};

struct ZipArchive;
using ZipArchiveHandle = ZipArchive*;

// AOSP dùng ZipString để bọc tên entry; ở đây chỉ cần chuyển tiếp const char*.
struct ZipString {
    ZipString() = default;
    explicit ZipString(const char* n) : name(n) {}
    const char* name = nullptr;
};

int32_t OpenArchiveFromMemory(void* address, size_t length, const char* debug_name,
                             ZipArchiveHandle* handle);
int32_t FindEntry(ZipArchiveHandle handle, const ZipString& entry_name, ZipEntry* data);
int32_t ExtractToMemory(ZipArchiveHandle handle, ZipEntry* entry, uint8_t* begin, size_t size);
void CloseArchive(ZipArchiveHandle handle);
const char* ErrorCodeString(int32_t error_code);

#endif  // KUDROID_ZIPARCHIVE_ZIP_ARCHIVE_H_
