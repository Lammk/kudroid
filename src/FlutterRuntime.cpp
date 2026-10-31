#include "kudroid/FlutterRuntime.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(KUDROID_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace kudroid {
namespace {

constexpr std::string_view kOriginalBuildId = "fb5eaead4cf4256da528a8ebbd653cdf89ec99bc";
constexpr std::string_view kCandidateBuildId = "d512a1385f6d73f28f618369888b65cef4fe6629";
constexpr std::string_view kCandidateSha256 = "df2715a416aac8dbcd63d81bc2171246af378e8dd9a92d2ece2f0b4489f6e3f1";
constexpr std::string_view kSnapshotHash = "830f4f59e7969c70b595182826435c19";
constexpr std::string_view kFeatures = "product no-code_comments no-dwarf_stack_traces_mode "
    "dedup_instructions no-tsan no-msan arm64 android compressed-pointers";

struct Header {
    std::uint8_t ident[16];
    std::uint16_t type, machine;
    std::uint32_t version;
    std::uint64_t entry, phoff, shoff;
    std::uint32_t flags;
    std::uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Program {
    std::uint32_t type, flags;
    std::uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
struct Section {
    std::uint32_t name, type;
    std::uint64_t flags, addr, offset, size;
    std::uint32_t link, info;
    std::uint64_t align, entsize;
};
struct Symbol {
    std::uint32_t name;
    std::uint8_t info, other;
    std::uint16_t shndx;
    std::uint64_t value, size;
};
static_assert(sizeof(Header) == 64 && sizeof(Program) == 56 &&
              sizeof(Section) == 64 && sizeof(Symbol) == 24);

std::string hex(const unsigned char* bytes, std::size_t size) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        result[2 * i] = digits[bytes[i] >> 4];
        result[2 * i + 1] = digits[bytes[i] & 15];
    }
    return result;
}

class ElfFile {
public:
    explicit ElfFile(const std::string& path) {
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return;
        struct stat statbuf {};
        if (::fstat(fd, &statbuf) == 0 && S_ISREG(statbuf.st_mode) &&
            statbuf.st_size >= static_cast<off_t>(sizeof(Header)) &&
            static_cast<std::uint64_t>(statbuf.st_size) <= std::numeric_limits<std::size_t>::max()) {
            size_ = static_cast<std::size_t>(statbuf.st_size);
            void* mapping = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapping != MAP_FAILED) data_ = static_cast<const unsigned char*>(mapping);
        }
        ::close(fd);
        if (!read(0, header_) || std::memcmp(header_.ident, "\177ELF\2\1\1", 7) != 0 ||
            header_.type != 3 || header_.machine != 183 || header_.version != 1 ||
            header_.ehsize != sizeof(Header) || header_.phentsize != sizeof(Program) ||
            header_.phnum == 0 || !range(header_.phoff, std::uint64_t(header_.phnum) * sizeof(Program))) return;
        for (std::uint16_t i = 0; i < header_.phnum; ++i) {
            Program program {};
            if (!read(header_.phoff + std::uint64_t(i) * sizeof(Program), program) ||
                !range(program.offset, program.filesz) ||
                (program.type == 1 && program.filesz > program.memsz)) return;
            programs_.push_back(program);
        }
        valid_ = true;
    }

    ~ElfFile() { if (data_) ::munmap(const_cast<unsigned char*>(data_), size_); }
    ElfFile(const ElfFile&) = delete;
    ElfFile& operator=(const ElfFile&) = delete;
    bool valid() const { return valid_; }

    std::string buildId() const {
        if (!valid_) return {};
        for (const auto& program : programs_) {
            if (program.type != 4) continue;
            std::uint64_t position = program.offset;
            const std::uint64_t end = program.offset + program.filesz;
            while (end - position >= 12) {
                std::uint32_t namesz = 0, descsz = 0, type = 0;
                if (!read(position, namesz) || !read(position + 4, descsz) || !read(position + 8, type)) return {};
                position += 12;
                const std::uint64_t nameBytes = (std::uint64_t(namesz) + 3) & ~std::uint64_t(3);
                const std::uint64_t descBytes = (std::uint64_t(descsz) + 3) & ~std::uint64_t(3);
                if (nameBytes > end - position || descBytes > end - position - nameBytes) return {};
                if (type == 3 && namesz == 4 && descsz == 20 &&
                    std::memcmp(data_ + position, "GNU\0", 4) == 0) {
                    return hex(data_ + position + nameBytes, descsz);
                }
                position += nameBytes + descBytes;
            }
        }
        return {};
    }

    bool snapshot(std::string_view name) const {
        Symbol symbol {};
        if (!findSymbol(name, symbol) || symbol.size < 53) return false;
        std::uint64_t offset = 0;
        if (!fileOffset(symbol.value, symbol.size, offset)) return false;
        std::uint32_t magic = 0;
        std::int64_t length = 0, kind = 0;
        if (!read(offset, magic) || !read(offset + 4, length) || !read(offset + 12, kind) ||
            magic != 0xdcdcf5f5 || kind != 3 || length < 49 ||
            std::uint64_t(length) > symbol.size - 4) return false;
        const std::uint64_t extent = std::uint64_t(length) + 4;
        if (std::memcmp(data_ + offset + 20, kSnapshotHash.data(), kSnapshotHash.size()) != 0) return false;
        std::string_view features;
        return stringAt(offset + 52, extent - 52, features) && features == kFeatures;
    }

    std::string sha256() const {
        if (!valid_) return {};
        std::array<unsigned char, 32> digest {};
#if defined(__APPLE__)
        if (size_ > std::numeric_limits<CC_LONG>::max() ||
            !CC_SHA256(data_, static_cast<CC_LONG>(size_), digest.data())) return {};
#elif defined(KUDROID_HAS_OPENSSL)
        unsigned int length = 0;
        if (EVP_Digest(data_, size_, digest.data(), &length, EVP_sha256(), nullptr) != 1 ||
            length != digest.size()) return {};
#else
        return {};
#endif
        return hex(digest.data(), digest.size());
    }

private:
    bool range(std::uint64_t offset, std::uint64_t length) const {
        return data_ && offset <= size_ && length <= size_ - offset;
    }
    template<class T> bool read(std::uint64_t offset, T& value) const {
        if (!range(offset, sizeof(T))) return false;
        std::memcpy(&value, data_ + offset, sizeof(T));
        return true;
    }
    bool stringAt(std::uint64_t offset, std::uint64_t length, std::string_view& result) const {
        if (!range(offset, length)) return false;
        const char* start = reinterpret_cast<const char*>(data_ + offset);
        const char* end = static_cast<const char*>(std::memchr(start, '\0', length));
        if (!end) return false;
        result = std::string_view(start, end - start);
        return true;
    }
    bool fileOffset(std::uint64_t address, std::uint64_t length, std::uint64_t& offset) const {
        for (const auto& program : programs_) {
            if (program.type != 1 || address < program.vaddr) continue;
            const std::uint64_t relative = address - program.vaddr;
            if (relative > program.filesz || length > program.filesz - relative) continue;
            offset = program.offset + relative;
            return range(offset, length);
        }
        return false;
    }
    bool findSymbol(std::string_view wanted, Symbol& result) const {
        if (!valid_ || header_.shentsize != sizeof(Section) || header_.shnum == 0 ||
            !range(header_.shoff, std::uint64_t(header_.shnum) * sizeof(Section))) return false;
        for (std::uint16_t i = 0; i < header_.shnum; ++i) {
            Section symbols {}, strings {};
            if (!read(header_.shoff + std::uint64_t(i) * sizeof(Section), symbols)) return false;
            if (symbols.type != 11) continue;
            if (symbols.entsize != sizeof(Symbol) || symbols.size % sizeof(Symbol) ||
                !range(symbols.offset, symbols.size) || symbols.link >= header_.shnum ||
                !read(header_.shoff + std::uint64_t(symbols.link) * sizeof(Section), strings) ||
                strings.type != 3 || !range(strings.offset, strings.size)) return false;
            for (std::uint64_t offset = 0; offset < symbols.size; offset += sizeof(Symbol)) {
                Symbol symbol {};
                std::string_view name;
                if (!read(symbols.offset + offset, symbol) || symbol.name >= strings.size ||
                    !stringAt(strings.offset + symbol.name, strings.size - symbol.name, name)) return false;
                const auto binding = symbol.info >> 4;
                const auto visibility = symbol.other & 3;
                if (name == wanted && symbol.shndx != 0 && (binding == 1 || binding == 2) &&
                    (visibility == 0 || visibility == 3)) {
                    result = symbol;
                    return true;
                }
            }
        }
        return false;
    }

    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
    Header header_ {};
    std::vector<Program> programs_;
    bool valid_ = false;
};

} // namespace

bool isConstrainedFlutterEligible(const std::string& originalPath,
                                 const std::string& candidatePath,
                                 std::string& reason) {
    ElfFile original(originalPath);
    if (!original.valid() || original.buildId() != kOriginalBuildId) {
        reason = "unsupported original engine";
        return false;
    }
    ElfFile app((std::filesystem::path(originalPath).parent_path() / "libapp.so").string());
    if (!app.snapshot("_kDartVmSnapshotData") || !app.snapshot("_kDartIsolateSnapshotData")) {
        reason = "incompatible or missing AOT snapshot";
        return false;
    }
    ElfFile candidate(candidatePath);
    if (!candidate.valid() || candidate.buildId() != kCandidateBuildId ||
        candidate.sha256() != kCandidateSha256) {
        reason = "missing or unverified runtime asset";
        return false;
    }
    reason = "Flutter 3.32.5 Android ARM64 product compressed-pointers";
    return true;
}

std::string bundledFlutterRuntimePath() {
#if defined(__APPLE__)
    CFURLRef url = CFBundleCopyResourceURL(CFBundleGetMainBundle(), CFSTR("libflutter"),
                                          CFSTR("so"), CFSTR("runtime"));
    if (!url) return {};
    std::array<UInt8, 4096> path {};
    const bool valid = CFURLGetFileSystemRepresentation(url, true, path.data(), path.size());
    CFRelease(url);
    if (valid) return reinterpret_cast<const char*>(path.data());
#else
    const char* configured = std::getenv("KUDROID_FLUTTER_RUNTIME");
    if (configured) return configured;
#endif
    return {};
}

} // namespace kudroid
