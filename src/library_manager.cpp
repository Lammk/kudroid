#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>

#if defined(KUDROID_HAS_MINIZIP)
#include <minizip/unzip.h>
#endif

namespace kudroid {
namespace {

#pragma pack(push, 1)
struct Elf64Ehdr {
    std::uint8_t ident[16];
    std::uint16_t type;
    std::uint16_t machine;
    std::uint32_t version;
    std::uint64_t entry;
    std::uint64_t phoff;
    std::uint64_t shoff;
    std::uint32_t flags;
    std::uint16_t ehsize;
    std::uint16_t phentsize;
    std::uint16_t phnum;
    std::uint16_t shentsize;
    std::uint16_t shnum;
    std::uint16_t shstrndx;
};

struct Elf64Phdr {
    std::uint32_t type;
    std::uint32_t flags;
    std::uint64_t offset;
    std::uint64_t vaddr;
    std::uint64_t paddr;
    std::uint64_t filesz;
    std::uint64_t memsz;
    std::uint64_t align;
};

struct Elf64Dyn {
    std::int64_t tag;
    std::uint64_t value;
};
#pragma pack(pop)

constexpr std::uint32_t PT_LOAD = 1;
constexpr std::uint32_t PT_DYNAMIC = 2;
constexpr std::int64_t DT_NULL = 0;
constexpr std::int64_t DT_NEEDED = 1;
constexpr std::int64_t DT_STRTAB = 5;
constexpr std::int64_t DT_STRSZ = 10;

bool readFile(const char* path, std::vector<char>& data) {
    if (!path) return false;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = file.tellg();
    if (size <= 0) return false;
    data.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(data.data(), size);
    return !!file;
}

} // namespace

std::vector<std::string> parse_elf_dependencies(const char* elfPath) {
    std::vector<std::string> result;
    std::vector<char> data;
    if (!readFile(elfPath, data) || data.size() < sizeof(Elf64Ehdr)) return result;

    const auto* header = reinterpret_cast<const Elf64Ehdr*>(data.data());
    if (header->ident[0] != 0x7f || header->ident[1] != 'E' ||
        header->ident[2] != 'L' || header->ident[3] != 'F' ||
        header->ident[4] != 2 || header->phentsize < sizeof(Elf64Phdr) ||
        header->phoff > data.size() ||
        header->phnum > (data.size() - header->phoff) / header->phentsize) {
        return result;
    }

    const Elf64Phdr* dynamicPhdr = nullptr;
    const auto* phdrBytes = reinterpret_cast<const std::uint8_t*>(data.data()) + header->phoff;
    auto vaddrToOffset = [&](std::uint64_t vaddr) -> std::uint64_t {
        for (std::uint16_t i = 0; i < header->phnum; ++i) {
            const auto* phdr = reinterpret_cast<const Elf64Phdr*>(phdrBytes + i * header->phentsize);
            if (phdr->type == PT_LOAD && vaddr >= phdr->vaddr &&
                vaddr - phdr->vaddr < phdr->filesz) {
                return phdr->offset + (vaddr - phdr->vaddr);
            }
        }
        return UINT64_MAX;
    };

    for (std::uint16_t i = 0; i < header->phnum; ++i) {
        const auto* phdr = reinterpret_cast<const Elf64Phdr*>(phdrBytes + i * header->phentsize);
        if (phdr->type == PT_DYNAMIC) {
            dynamicPhdr = phdr;
            break;
        }
    }
    if (!dynamicPhdr || dynamicPhdr->offset > data.size() ||
        dynamicPhdr->filesz > data.size() - dynamicPhdr->offset) return result;

    const auto* dynamic = reinterpret_cast<const Elf64Dyn*>(data.data() + dynamicPhdr->offset);
    const std::size_t dynamicCount = dynamicPhdr->filesz / sizeof(Elf64Dyn);
    std::uint64_t strtabVaddr = 0;
    std::uint64_t strtabSize = 0;
    std::vector<std::uint64_t> neededOffsets;
    for (std::size_t i = 0; i < dynamicCount && dynamic[i].tag != DT_NULL; ++i) {
        if (dynamic[i].tag == DT_NEEDED) neededOffsets.push_back(dynamic[i].value);
        else if (dynamic[i].tag == DT_STRTAB) strtabVaddr = dynamic[i].value;
        else if (dynamic[i].tag == DT_STRSZ) strtabSize = dynamic[i].value;
    }

    const std::uint64_t strtabOffset = vaddrToOffset(strtabVaddr);
    if (strtabOffset == UINT64_MAX || strtabOffset > data.size() ||
        strtabSize > data.size() - strtabOffset) return result;
    const char* strtab = data.data() + strtabOffset;
    for (const auto neededOffset : neededOffsets) {
        if (neededOffset >= strtabSize) continue;
        const char* name = strtab + neededOffset;
        const std::size_t remaining = static_cast<std::size_t>(strtabSize - neededOffset);
        if (std::find(name, name + remaining, '\0') != name + remaining) {
            result.emplace_back(name);
        }
    }
    return result;
}

bool LibraryManager::loadRecursive(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path normalized = fs::weakly_canonical(path, error);
    const std::string key = (error ? fs::path(path).lexically_normal() : normalized).string();
    const std::string soname = fs::path(key).filename().string();
    if (libraries_.count(key) || libraries_.count(soname)) {
        std::fprintf(stderr, "[kudroid_core] Dependency already loaded: %s\n", key.c_str());
        return true;
    }

    std::fprintf(stderr, "[kudroid_core] Loading ELF recursively: %s\n", key.c_str());
    auto loader = std::make_unique<ElfLoader>(key);
    loader->setLibraryManager(this);
    if (!loader->parse()) {
        lastError_ = "Parse failed for " + key + ": " + loader->lastError();
        std::fprintf(stderr, "[kudroid_core] %s\n", lastError_.c_str());
        return false;
    }

    const auto dependencies = parse_elf_dependencies(key.c_str());
    std::fprintf(stderr, "[kudroid_core] %s has %zu DT_NEEDED entries\n",
                 soname.c_str(), dependencies.size());
    libraries_.emplace(key, std::move(loader));
    ElfLoader* current = libraries_.at(key).get();

    for (const auto& dependency : dependencies) {
        std::fprintf(stderr, "[kudroid_core] DT_NEEDED: %s -> %s\n",
                     soname.c_str(), dependency.c_str());
        const fs::path dependencyPath = fs::path(key).parent_path() / dependency;
        if (!fs::exists(dependencyPath)) {
            std::fprintf(stderr, "[kudroid_core] Dependency not extracted; using shim/global fallback: %s\n",
                         dependency.c_str());
            continue;
        }
        // Gọi đệ quy cần mở khóa nếu re-entrant hoặc tự kiểm tra map. Ở đây loadRecursive giữ lock nội bộ an toàn.
        const fs::path depNorm = fs::weakly_canonical(dependencyPath.string(), error);
        const std::string depKey = (error ? dependencyPath.lexically_normal() : depNorm).string();
        const std::string depSoname = fs::path(depKey).filename().string();
        if (libraries_.count(depKey) || libraries_.count(depSoname)) {
            continue;
        }
        auto depLoader = std::make_unique<ElfLoader>(depKey);
        depLoader->setLibraryManager(this);
        if (depLoader->parse()) {
            libraries_.emplace(depKey, std::move(depLoader));
        }
    }

    if (!current->map() || !current->relocate()) {
        lastError_ = "Load failed for " + key + ": " + current->lastError();
        std::fprintf(stderr, "[kudroid_core] %s\n", lastError_.c_str());
        return false;
    }
    
    current->registerEhFrame();
    current->executeInit();
    
    std::fprintf(stderr, "[kudroid_core] Loaded ELF successfully: %s\n", key.c_str());
    return true;
}

// libraries_ là unordered_map — iteration order KHÔNG xác định. Hàm này trả
// danh sách key đã sắp xếp để mọi thứ (resolve symbol, lặp thư viện) chạy theo
// thứ tự ổn định giữa các lần chạy.
static std::vector<std::string> sortedLibraryKeys(const std::unordered_map<std::string, std::unique_ptr<kudroid::ElfLoader>>& libs) {
    std::vector<std::string> keys;
    keys.reserve(libs.size());
    for (const auto& entry : libs) keys.push_back(entry.first);
    std::sort(keys.begin(), keys.end());
    return keys;
}

// libapng-drawable static-link một bản libc++abi riêng và EXPORT __cxa_guard_*
// → mọi module resolve về bản đó (log "resolved from libapng-drawable.so").
// Shim guard của ta (xử lý same-tid recursion thay vì abort) phải thắng —
// special-case đúng 3 symbol này, mọi thứ khác giữ nguyên thứ tự cũ.
static bool isGuardShimSymbol(const char* name) {
    return strcmp(name, "__cxa_guard_acquire") == 0 ||
           strcmp(name, "__cxa_guard_release") == 0 ||
           strcmp(name, "__cxa_guard_abort") == 0;
}

void* LibraryManager::resolveGlobalSymbol(const char* name) const {
    if (!name || !*name) return nullptr;
    if (isGuardShimSymbol(name)) {
        void* shim = resolve_bionic_symbol(name);
        if (shim) return shim;
    }
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    for (const auto& key : sortedLibraryKeys(libraries_)) {
        void* address = libraries_.at(key)->getSymbolAddress(name);
        if (address) {
            std::fprintf(stderr, "[kudroid_core] Global symbol %s resolved from %s -> %p\n",
                         name, key.c_str(), address);
            return address;
        }
    }
    return resolve_bionic_symbol(name);
}

void* LibraryManager::resolveAppSymbol(const char* name) {
    if (!name || !*name) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    for (const auto& key : sortedLibraryKeys(libraries_)) {
        void* address = libraries_.at(key)->getSymbolAddress(name);
        if (address) {
            std::fprintf(stderr, "[kudroid_core] App symbol %s resolved from %s -> %p\n",
                         name, key.c_str(), address);
            return address;
        }
    }
    return nullptr;
}

std::vector<std::pair<std::string, void*>> LibraryManager::resolveAllSymbols(const char* name) const {
    std::vector<std::pair<std::string, void*>> result;
    if (!name || !*name) return result;
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    for (const auto& key : sortedLibraryKeys(libraries_)) {
        void* address = libraries_.at(key)->getSymbolAddress(name);
        if (address) {
            std::fprintf(stderr, "[kudroid_core] Symbol %s resolved from %s -> %p\n",
                         name, key.c_str(), address);
            result.emplace_back(key, address);
        }
    }
    return result;
}

bool extract_arm64_libs_from_apk(const char* apkPath, const char* outputDirectory,
                                 std::string* error) {
#if !defined(KUDROID_HAS_MINIZIP)
    if (error) *error = "minizip is not linked; APK extraction is unavailable";
    (void)apkPath;
    (void)outputDirectory;
    return false;
#else
    if (!apkPath || !outputDirectory) {
        if (error) *error = "APK path or output directory is null";
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code fsError;
    fs::create_directories(outputDirectory, fsError);
    if (fsError) {
        if (error) *error = "Cannot create output directory: " + fsError.message();
        return false;
    }

    unzFile archive = unzOpen64(apkPath);
    if (!archive) {
        if (error) *error = "Cannot open APK";
        return false;
    }
    bool success = true;
    if (unzGoToFirstFile(archive) == UNZ_OK) {
        do {
            char name[1024] = {};
            unz_file_info64 info = {};
            if (unzGetCurrentFileInfo64(archive, &info, name, sizeof(name), nullptr, 0,
                                        nullptr, 0) != UNZ_OK) {
                success = false;
                break;
            }
            const std::string entry(name);
            constexpr const char* prefix = "lib/arm64-v8a/";
            if (entry.rfind(prefix, 0) != 0 || entry.size() < 3 ||
                entry.substr(entry.size() - 3) != ".so") continue;
            if (unzOpenCurrentFile(archive) != UNZ_OK) {
                success = false;
                break;
            }
            const fs::path destination = fs::path(outputDirectory) /
                                         fs::path(entry).filename();
            std::ofstream output(destination, std::ios::binary);
            char buffer[16384];
            int bytesRead = 0;
            while ((bytesRead = unzReadCurrentFile(archive, buffer, sizeof(buffer))) > 0) {
                output.write(buffer, bytesRead);
            }
            unzCloseCurrentFile(archive);
            if (bytesRead < 0 || !output) {
                success = false;
                break;
            }
            std::fprintf(stderr, "[kudroid_core] Extracted APK library: %s\n",
                         destination.string().c_str());
        } while (unzGoToNextFile(archive) == UNZ_OK);
    }
    unzClose(archive);
    if (!success && error) *error = "Failed while extracting APK libraries";
    return success;
#endif
}

} // namespace kudroid
