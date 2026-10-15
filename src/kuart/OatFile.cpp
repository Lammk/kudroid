#include "kudroid/kuart/OatFile.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

namespace kudroid {
namespace kuart {
namespace {

// On-disk header in host byte order; the file never leaves this device.
struct Header {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t reserved;  // keeps the record array 8-byte aligned
};

struct Record {
    uint32_t dex_checksum;
    uint32_t method_index;
    uint8_t state;
    uint8_t pad[3];
};

static_assert(sizeof(Header) == 16, "OAT header layout is on-disk state");
static_assert(sizeof(Record) == 12, "OAT record layout is on-disk state");

}  // namespace

bool OatFile::Load(const std::string& path) {
    entries_.clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;

    Header h = {};
    if (std::fread(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    // Wrong magic/version means another build; treat as absent.
    if (h.magic != kMagic || h.version != kVersion) {
        std::fclose(f);
        return false;
    }
    if (h.entry_count > kMaxEntries) {
        std::fclose(f);
        return false;
    }

    entries_.reserve(h.entry_count);
    for (uint32_t i = 0; i < h.entry_count; ++i) {
        Record r = {};
        if (std::fread(&r, sizeof(r), 1, f) != 1) {
            // Truncated: keep the partial profile.
            break;
        }
        if (r.state > static_cast<uint8_t>(MethodState::kRefused)) continue;
        entries_[Key{r.dex_checksum, r.method_index}] =
            static_cast<MethodState>(r.state);
    }
    std::fclose(f);
    return true;
}

bool OatFile::Save(const std::string& path) const {
    // Write to a temp file then rename so a crash keeps the old file.
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) return false;

    Header h = {};
    h.magic = kMagic;
    h.version = kVersion;
    h.entry_count = static_cast<uint32_t>(entries_.size());
    h.reserved = 0;
    if (std::fwrite(&h, sizeof(h), 1, f) != 1) {
        std::fclose(f);
        std::remove(tmp.c_str());
        return false;
    }

    for (const auto& kv : entries_) {
        Record r = {};
        r.dex_checksum = kv.first.checksum;
        r.method_index = kv.first.index;
        r.state = static_cast<uint8_t>(kv.second);
        if (std::fwrite(&r, sizeof(r), 1, f) != 1) {
            std::fclose(f);
            std::remove(tmp.c_str());
            return false;
        }
    }
    std::fclose(f);

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

void OatFile::Note(uint32_t dex_checksum, uint32_t method_index, MethodState state) {
    if (state == MethodState::kUnknown) return;
    const Key key{dex_checksum, method_index};
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second = state;
        return;
    }
    // At the cap, keep proven entries over new guesses.
    if (entries_.size() >= kMaxEntries) return;
    entries_.emplace(key, state);
}

OatFile::MethodState OatFile::Lookup(uint32_t dex_checksum, uint32_t method_index) const {
    const auto it = entries_.find(Key{dex_checksum, method_index});
    return it != entries_.end() ? it->second : MethodState::kUnknown;
}

void OatFile::InvalidateChecksum(uint32_t dex_checksum) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.checksum == dex_checksum) it = entries_.erase(it);
        else ++it;
    }
}

}  // namespace kuart
}  // namespace kudroid
