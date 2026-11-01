#include "kudroid/RuntimeCageAdapter.h"
#include "kudroid/RuntimeCageApi.h"
#include "kudroid/AlignedVmReservations.h"
#include "kudroid/FlutterRuntime.h"
#include "kudroid/elf_loader.hpp"
#include "../third_party/picojson/picojson.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(KUDROID_HAS_OPENSSL)
#include <openssl/evp.h>
#endif

namespace kudroid {
namespace {
constexpr const char* kInit = "_ZN2v88internal17VirtualMemoryCage15InitReservationERKNS1_17ReservationParamsE";
constexpr const char* kVersion = "_ZN2v82V810GetVersionEv";
constexpr const char* kSharedCage = "_ZN2v88internal16IsolateAllocator24InitializeOncePerProcessEv";

struct Installation {
    std::shared_ptr<AlignedVmReservations> reservations;
    std::unique_ptr<ElfLoader> adapter;
};

struct Asset {
    std::string path;
    uint64_t length = 0;
    uint64_t alignment = 0;
};

struct EarlyPool {
    std::mutex mutex;
    Asset asset;
    std::shared_ptr<AlignedVmReservations> reservations;
    bool attempted = false;
    bool prepared = false;
};

EarlyPool& earlyPool() {
    static EarlyPool pool;
    return pool;
}

std::string directory() {
#if defined(__APPLE__)
    CFURLRef url = CFBundleCopyResourceURL(CFBundleGetMainBundle(), CFSTR("runtime-adapters"), nullptr, nullptr);
    if (!url) return {};
    std::array<UInt8, 4096> path{};
    const bool valid = CFURLGetFileSystemRepresentation(url, true, path.data(), path.size());
    CFRelease(url);
    return valid ? reinterpret_cast<const char*>(path.data()) : std::string();
#else
    const char* configured = std::getenv("KUDROID_RUNTIME_ADAPTER_DIRECTORY");
    return configured ? configured : std::string();
#endif
}

std::string digest(const std::string& data) {
    std::array<unsigned char, 32> bytes{};
#if defined(__APPLE__)
    if (data.size() > UINT32_MAX || !CC_SHA256(data.data(), data.size(), bytes.data())) return {};
#elif defined(KUDROID_HAS_OPENSSL)
    unsigned size = 0;
    if (!EVP_Digest(data.data(), data.size(), bytes.data(), &size, EVP_sha256(), nullptr) || size != bytes.size()) return {};
#else
    return {};
#endif
    const char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : bytes) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}

Asset checkedAsset() {
    const std::filesystem::path root = directory();
    if (root.empty()) return {};
    std::ifstream manifestFile(root / "v8-9.4.146.24.json", std::ios::binary);
    std::string manifest{std::istreambuf_iterator<char>(manifestFile), {}};
    if (manifest.empty() || manifest.size() > 65536) return {};
    picojson::value parsed;
    if (!picojson::parse(parsed, manifest).empty() || !parsed.is<picojson::object>()) return {};
    const auto& record = parsed.get<picojson::object>();
    auto field = [&](const char* name) -> std::string {
        auto it = record.find(name);
        return it != record.end() && it->second.is<std::string>() ? it->second.get<std::string>() : std::string();
    };
    auto integer = [&](const char* name) -> uint64_t {
        auto it = record.find(name);
        if (it == record.end() || !it->second.is<double>()) return 0;
        const double value = it->second.get<double>();
        if (!std::isfinite(value) || value <= 0 || value >= static_cast<double>(SIZE_MAX) ||
            std::trunc(value) != value) return 0;
        return static_cast<uint64_t>(value);
    };
    const uint64_t length = integer("early_length"), alignment = integer("early_alignment");
    if (integer("api_version") != 1 || !length || !alignment || (alignment & (alignment - 1))) return {};
    const std::string filename = field("file");
    if (field("v8_version") != "9.4.146.24" || filename != "v8-9.4.146.24.so") return {};
    const auto path = root / filename;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || !size || size > 16 * 1024 * 1024) return {};
    std::ifstream input(path, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input), {}};
    const auto actual = digest(bytes);
    return !actual.empty() && actual == field("sha256") ? Asset{path.string(), length, alignment} : Asset{};
}

void* reserve(void* context, uint64_t length, uint64_t alignment, void* hint) {
    auto& owner = *static_cast<Installation*>(context)->reservations;
    void* address = owner.reserve(length, alignment, hint);
    std::fprintf(stderr, "[KuDroidCage] reserve length=%llu alignment=%llu base=%p\n",
        static_cast<unsigned long long>(length), static_cast<unsigned long long>(alignment), address);
    return address;
}
int release(void* context, void* address, uint64_t length) {
    return static_cast<Installation*>(context)->reservations->release(address, length);
}
int protect(void* context, void* address, uint64_t length, int permissions) {
    return static_cast<Installation*>(context)->reservations->protect(address, length, permissions);
}
int discard(void* context, void* address, uint64_t length) {
    return static_cast<Installation*>(context)->reservations->discard(address, length);
}
}

void prepareRuntimeCageReservations() {
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
    auto& pool = earlyPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    if (pool.attempted) return;
    pool.attempted = true;
    pool.asset = checkedAsset();
    if (pool.asset.path.empty()) return;
    pool.reservations = std::make_shared<AlignedVmReservations>();
    pool.prepared = pool.reservations->prepare(pool.asset.length, pool.asset.alignment);
#endif
}

void logEarlyRuntimeCageReservations() {
#if defined(__APPLE__) && (defined(__arm64__) || defined(__aarch64__))
    auto& pool = earlyPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    std::fprintf(stderr, "[KuDroidCage] phase=native-initializer source=9.4.146.24 length=%llu alignment=%llu prepared=%d\n",
        static_cast<unsigned long long>(pool.asset.length),
        static_cast<unsigned long long>(pool.asset.alignment), pool.prepared);
#endif
}

bool installRuntimeCageAdapter(LibraryManager& manager, ElfLoader& runtime) {
#if !defined(__APPLE__) || (!defined(__arm64__) && !defined(__aarch64__))
    // Host ELF tests inspect ARM64 images without executing their code.
    (void)manager;
    (void)runtime;
    return true;
#else
    void* original = runtime.getSymbolAddress(kInit);
    void* version = runtime.getSymbolAddress(kVersion);
    if (!original || !version || !runtime.getSymbolAddress(kSharedCage)) return true;
    const char* reported = reinterpret_cast<const char* (*)()>(version)();
    if (!reported || std::strcmp(reported, "9.4.146.24")) return true;
    for (const char* name : {"_ZN2v84base20BoundedPageAllocatorC1EPNS_13PageAllocatorEmmm",
            "_ZN2v88internal13VirtualMemory5ResetEv", "_ZN2v88internal13VirtualMemoryD1Ev"}) {
        if (!runtime.getSymbolAddress(name)) return true;
    }
    const Asset asset = checkedAsset();
    if (asset.path.empty()) {
        std::fprintf(stderr, "[KuDroidCage] adapter asset missing or unverified\n");
        return true;
    }
    auto owner = std::make_shared<Installation>();
    owner->adapter = std::make_unique<ElfLoader>(asset.path);
    owner->adapter->setLibraryManager(&manager);
    owner->adapter->setImportScope(&runtime);
    if (!owner->adapter->parse() || !owner->adapter->map() || !owner->adapter->relocate()) {
        std::fprintf(stderr, "[KuDroidCage] adapter load failed: %s\n", owner->adapter->lastError());
        return true;
    }
    void* replacement = owner->adapter->getSymbolAddress(kInit);
    void* configure = owner->adapter->getSymbolAddress("kudroid_v8_cage_configure");
    if (!replacement || !configure) return true;
    {
        auto& pool = earlyPool();
        std::lock_guard<std::mutex> lock(pool.mutex);
        if (pool.asset.path == asset.path && pool.asset.length == asset.length &&
            pool.asset.alignment == asset.alignment) owner->reservations = std::move(pool.reservations);
    }
    if (!owner->reservations) owner->reservations = std::make_shared<AlignedVmReservations>();
    KudroidRuntimeCageApi api{1, owner->reservations->pageSize(), owner.get(), original,
                             reserve, release, protect, discard};
    uint64_t length = 0, alignment = 0;
    if (!reinterpret_cast<int (*)(const KudroidRuntimeCageApi*, uint64_t*, uint64_t*)>(configure)(
            &api, &length, &alignment)) return true;
    if (length != asset.length || alignment != asset.alignment) {
        std::fprintf(stderr, "[KuDroidCage] source requirements disagree with metadata\n");
        return true;
    }
    const bool prepared = owner->reservations->prepare(length, alignment);
    if (!runtime.interposeFunction(kInit, replacement)) {
        std::fprintf(stderr, "[KuDroidCage] runtime has no preemptable cage binding\n");
        return true;
    }
    owner->adapter->releaseFileMapping();
    runtime.retainRuntimeAttachment(owner);
    std::fprintf(stderr, "[KuDroidCage] source-adapter=9.4.146.24 early-length=%llu alignment=%llu prepared=%d page=%zu\n",
        static_cast<unsigned long long>(length), static_cast<unsigned long long>(alignment),
        prepared, owner->reservations->pageSize());
    return true;
#endif
}
}
