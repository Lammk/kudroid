#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace kudroid {

// cấu trúc dexheader cơ bản (được đơn giản hóa)
struct DexHeader {
    uint8_t  magic[8];
    uint32_t checksum;
    uint8_t  signature[20];
    uint32_t fileSize;
    uint32_t headerSize;
    uint32_t endianTag;
    uint32_t linkSize;
    uint32_t linkOff;
    uint32_t mapOff;
    uint32_t stringIdsSize;
    uint32_t stringIdsOff;
    uint32_t typeIdsSize;
    uint32_t typeIdsOff;
    uint32_t protoIdsSize;
    uint32_t protoIdsOff;
    uint32_t fieldIdsSize;
    uint32_t fieldIdsOff;
    uint32_t methodIdsSize;
    uint32_t methodIdsOff;
    uint32_t classDefsSize;
    uint32_t classDefsOff;
    uint32_t dataSize;
    uint32_t dataOff;
};

class DexFile {
public:
    DexFile(const std::string& path);
    ~DexFile();

    bool load();
    const DexHeader* getHeader() const;
    void* getBaseAddress() const;
    const std::string& getPath() const { return path_; }

private:
    std::string path_;
    void* baseAddr_ = nullptr;
    size_t size_ = 0;
};

class DexManager {
public:
    static DexManager& getInstance();
    
    // quét thư mục để tìm các tệp .dex và tải chúng
    bool loadDirectory(const std::string& dirPath);
    
    // ánh xạ một tệp .dex vào bộ nhớ có thể thực thi
    bool mapDexFile(const std::string& path);
    
    const std::vector<DexFile*>& getLoadedDexFiles() const { return dexFiles_; }

private:
    DexManager() = default;
    ~DexManager();
    
    std::vector<DexFile*> dexFiles_;
};

// api giả cho bản dịch jit trong tương lai
extern "C" void kudroid_dex_jit_compile(DexFile* dexFile);

} // namespace kudroid
