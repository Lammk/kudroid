#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kudroid {

/// Minimal ELF64 (ARM64) loader surface.
/// Phase 1 entry point — parsing, mapping, and relocation stubs.
class ElfLoader {
public:
    struct Segment {
        std::uint64_t vaddr  = 0;   ///< Target virtual address
        std::uint64_t offset = 0;   ///< File offset of PT_LOAD data
        std::uint64_t filesz = 0;   ///< Bytes in file
        std::uint64_t memsz  = 0;   ///< Bytes in memory (>= filesz)
        std::uint32_t flags  = 0;   ///< PF_R | PF_W | PF_X
    };

    explicit ElfLoader(std::string path);
    ~ElfLoader();

    // Non-copyable, movable
    ElfLoader(const ElfLoader&) = delete;
    ElfLoader& operator=(const ElfLoader&) = delete;
    ElfLoader(ElfLoader&&) noexcept;
    ElfLoader& operator=(ElfLoader&&) noexcept;

    /// Parse ELF64 headers and populate segment list.
    bool parse();

    /// Map PT_LOAD segments into executable memory.
    bool map();

    /// Apply relocations and resolve dynamic symbols.
    bool relocate();

    [[nodiscard]] std::uint64_t entryPoint() const { return entry_; }
    [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }
    [[nodiscard]] bool isLoaded() const { return base_ != nullptr; }

private:
    std::string          path_;
    void*                base_     = nullptr;
    std::uint64_t        entry_    = 0;
    std::vector<Segment> segments_;
};

} // namespace kudroid