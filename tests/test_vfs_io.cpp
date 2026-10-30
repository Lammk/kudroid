#include "kudroid/VFSPathRemapper.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

namespace {
int checks = 0;
int failures = 0;

void Check(bool ok, const char* description) {
    ++checks;
    if (!ok) ++failures;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", description);
}

void Put32(unsigned char* bytes, size_t offset, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset++] = value >> shift;
}

std::vector<unsigned char> Bank(size_t count, size_t blobSize) {
    std::vector<unsigned char> bytes(count * blobSize, 0);
    for (size_t index = 0; index < count; ++index) {
        unsigned char* header = bytes.data() + index * blobSize;
        std::memcpy(header, "FSB5", 4);
        Put32(header, 4, 1);
        Put32(header, 8, 1);
        Put32(header, 12, 16);
        Put32(header, 20, blobSize - 76);
        Put32(header, 24, 15);
        Put32(header, 68, (11u << 25) | (4u << 1));
        Put32(header, 72, 0x12345678);
        std::memset(header + 76, 1 + index % 250, blobSize - 76);
    }
    return bytes;
}

void Write(const std::string& path, const std::vector<unsigned char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    Check(file.good(), "stream fixture is written completely");
}

void SeekAndRead(const std::string& path, const std::vector<unsigned char>& bytes) {
    FILE* stream = kudroid::vfs_fopen(path.c_str(), "rb");
    Check(stream != nullptr, "tracked archive stream opens");
    if (stream == nullptr) return;
    Check(kudroid::vfs_fseeko(stream, 1, SEEK_SET) == 0, "absolute seek sets the cursor");
    errno = 0;
    const int overflow = kudroid::vfs_fseeko(stream, std::numeric_limits<off_t>::max(), SEEK_CUR);
    Check(overflow < 0 && errno == EOVERFLOW, "relative seek overflow reports EOVERFLOW");
    Check(kudroid::vfs_ftello(stream) == 1, "overflow leaves the cursor intact");
    errno = 0;
    const int endOverflow = kudroid::vfs_fseeko(stream, std::numeric_limits<off_t>::max(), SEEK_END);
    Check(endOverflow < 0 && errno == EOVERFLOW, "end-relative seek overflow reports EOVERFLOW");
    Check(kudroid::vfs_ftello(stream) == 1, "end-relative overflow leaves the cursor intact");
    errno = 0;
    Check(kudroid::vfs_fseeko(stream, -2, SEEK_CUR) < 0 && errno == EINVAL,
          "negative resulting cursor reports EINVAL");
    Check(kudroid::vfs_fseeko(stream, -5, SEEK_END) == 0, "end-relative seek finds the tail");
    unsigned char tail[8] = {};
    Check(kudroid::vfs_fread(tail, 4, 2, stream) == 1, "partial final element is not counted");
    Check(std::memcmp(tail, bytes.data() + bytes.size() - 5, 5) == 0,
          "partial final element still consumes and copies its bytes");
    Check(kudroid::vfs_ftello(stream) == static_cast<off_t>(bytes.size())
          && kudroid::vfs_feof(stream), "partial read reaches the shared cursor and EOF");
    Check(kudroid::vfs_fseeko(stream, 0, SEEK_CUR) == 0 && !kudroid::vfs_feof(stream),
          "successful seek clears EOF");
    Check(kudroid::vfs_fseeko(stream, 0, SEEK_END) == 0 && !kudroid::vfs_feof(stream),
          "positioning at the end does not set EOF before reading");
    Check(kudroid::vfs_fseeko(stream, 0, SEEK_SET) == 0, "stream can rewind for an exact-sized read");
    std::vector<unsigned char> entire(bytes.size());
    Check(kudroid::vfs_fread(entire.data(), 1, entire.size(), stream) == entire.size()
          && entire == bytes && !kudroid::vfs_feof(stream),
          "reading exactly to the end does not claim a failed read");
    Check(kudroid::vfs_fread(tail, 1, 1, stream) == 0 && kudroid::vfs_feof(stream),
          "reading past the end sets EOF");
    const off_t position = kudroid::vfs_ftello(stream);
    Check(kudroid::vfs_fread(tail, 0, 10, stream) == 0 && kudroid::vfs_ftello(stream) == position,
          "zero-size read does not move the cursor");
    Check(kudroid::vfs_fclose(stream) == 0, "tracked stream closes");
}

void HeaderBoundary(const std::string& path, const std::vector<unsigned char>& bytes) {
    const long page = ::sysconf(_SC_PAGESIZE);
    Check(page >= 128, "guard page can hold the diagnostic header");
    if (page < 128) return;
    void* memory = ::mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    Check(memory != MAP_FAILED, "header guard buffer is mapped");
    if (memory == MAP_FAILED) return;
    const bool protectedPage = ::mprotect(static_cast<char*>(memory) + page, page, PROT_NONE) == 0;
    Check(protectedPage, "bytes after the header buffer are inaccessible");
    if (protectedPage) {
        constexpr size_t lengths[] = {4, 27, 28, 59, 60, 68, 72, 76, 128};
        for (const size_t length : lengths) {
            FILE* stream = kudroid::vfs_fopen(path.c_str(), "rb");
            Check(stream != nullptr, "header stream opens");
            if (!stream) continue;
            auto* header = static_cast<unsigned char*>(memory) + page - length;
            Check(kudroid::vfs_fread(header, 1, length, stream) == length,
                  "diagnostic cannot read beyond a partial or complete served header");
            Check(std::memcmp(header, bytes.data(), length) == 0,
                  "diagnostic read preserves every returned byte");
            kudroid::vfs_fclose(stream);
        }
    }
    ::munmap(memory, page * 2);
}

int Benchmark(const std::string& path, const std::vector<unsigned char>& bytes, int passes) {
    FILE* stream = kudroid::vfs_fopen(path.c_str(), "rb");
    if (!stream) return 1;
    std::vector<unsigned char> chunk(2048);
    uint64_t checksum = 0, expected = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int pass = 0; pass < passes; ++pass) {
        if (kudroid::vfs_fseeko(stream, 0, SEEK_SET) != 0) return 1;
        for (size_t offset = 0; offset < bytes.size(); offset += chunk.size()) {
            if (kudroid::vfs_fread(chunk.data(), 1, chunk.size(), stream) != chunk.size()) return 1;
            checksum += chunk.front() + chunk.back();
            expected += bytes[offset] + bytes[offset + chunk.size() - 1];
        }
    }
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    kudroid::vfs_fclose(stream);
    std::printf("IO benchmark: %.3f ms, bytes=%zu, checksum=%llu, expected=%llu\n", elapsed,
                bytes.size() * passes, static_cast<unsigned long long>(checksum),
                static_cast<unsigned long long>(expected));
    return checksum == expected ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const auto directory = std::filesystem::temp_directory_path()
            / ("kudroid_vfs_io_" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    auto& remapper = kudroid::VFSPathRemapper::getInstance();
    remapper.setDocumentsDirectory((directory / "Documents").string());
    const std::string path = remapper.remap("/data/app/io-probe/bank.apk");
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    int status = 0;
    if (argc == 3 && std::strcmp(argv[1], "--bench") == 0) {
        const int passes = std::atoi(argv[2]);
        if (passes < 1 || passes > 1000) return 1;
        const auto bytes = Bank(128, 65536);
        Write(path, bytes);
        status = Benchmark(path, bytes, passes);
    } else {
        const auto bytes = Bank(1, 140);
        Write(path, bytes);
        if (argc == 1) SeekAndRead(path, bytes);
        HeaderBoundary(path, bytes);
        std::printf("%d checks, %d failures\n", checks, failures);
        status = failures != 0;
    }
    std::filesystem::remove_all(directory);
    return status;
}
