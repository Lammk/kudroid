#include "kudroid/DexManager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <iostream>

namespace kudroid {

DexFile::DexFile(const std::string& path) : path_(path) {
}

DexFile::~DexFile() {
    if (baseAddr_ && baseAddr_ != MAP_FAILED) {
        munmap(baseAddr_, size_);
    }
}

bool DexFile::load() {
    int fd = open(path_.c_str(), O_RDONLY);
    if (fd < 0) {
        std::fprintf(stderr, "[kudroid_dex] Failed to open DEX file: %s\n", path_.c_str());
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return false;
    }
    size_ = st.st_size;

    if (size_ < sizeof(DexHeader)) {
        std::fprintf(stderr, "[kudroid_dex] File too small to be a DEX file: %s\n", path_.c_str());
        close(fd);
        return false;
    }

    // ánh xạ vào bộ nhớ với prot_read | prot_exec để mô phỏng vùng thực thi jit sau này
    // trong môi trường jit thực tế, kết quả phân tích sẽ là prot_exec. hiện tại, chúng ta chỉ ánh xạ dex ở chế độ chỉ đọc.
    baseAddr_ = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (baseAddr_ == MAP_FAILED) {
        std::fprintf(stderr, "[kudroid_dex] mmap failed for: %s\n", path_.c_str());
        return false;
    }

    const DexHeader* header = getHeader();
    if (memcmp(header->magic, "dex\n", 4) != 0) {
        std::fprintf(stderr, "[kudroid_dex] Invalid DEX magic in: %s\n", path_.c_str());
        return false;
    }

    std::fprintf(stderr, "[kudroid_dex] Loaded %s (size: %zu bytes, classes: %u)\n", 
                 path_.c_str(), size_, header->classDefsSize);

    // gọi hook jit giả
    kudroid_dex_jit_compile(this);

    return true;
}

const DexHeader* DexFile::getHeader() const {
    return reinterpret_cast<const DexHeader*>(baseAddr_);
}

void* DexFile::getBaseAddress() const {
    return baseAddr_;
}

DexManager& DexManager::getInstance() {
    static DexManager instance;
    return instance;
}

DexManager::~DexManager() {
    for (auto* dex : dexFiles_) {
        delete dex;
    }
    dexFiles_.clear();
}

bool DexManager::loadDirectory(const std::string& dirPath) {
    if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath)) {
        return false;
    }

    bool loadedAny = false;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
        if (entry.is_regular_file()) {
            std::string path = entry.path().string();
            if (path.size() >= 4 && path.compare(path.size() - 4, 4, ".dex") == 0) {
                if (mapDexFile(path)) {
                    loadedAny = true;
                }
            }
        }
    }
    return loadedAny;
}

bool DexManager::mapDexFile(const std::string& path) {
    auto* dexFile = new DexFile(path);
    if (dexFile->load()) {
        dexFiles_.push_back(dexFile);
        return true;
    }
    delete dexFile;
    return false;
}

extern "C" void kudroid_dex_jit_compile(DexFile* dexFile) {
    // hook biên dịch jit giả
    std::fprintf(stderr, "[kudroid_jit] JIT hook called for DEX: %s (Simulating AOT/JIT parsing...)\n", 
                 dexFile->getPath().c_str());
    // ... sẽ được triển khai trong tương lai ...
}

} // namespace kudroid
