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
    [[nodiscard]] bool isParsed() const { return parsed_; }

    /// Returns the last error message (empty if no error).
    [[nodiscard]] const char* lastError() const;

    /// Phase 2: Look up a symbol address from .dynsym
    /// @return  Function pointer (base_ + st_value), or nullptr if not found.
    void* getSymbolAddress(const char* symbolName);

    /// Phase 2: Test execution – call kudroid_add(40, 20) via dynamic symbol.
    /// @return  Result string with status and computed value.
    std::string testExecution();

private:
    // Internal: read file contents into buffer
    bool readFile(std::vector<char>& buf);

    std::string          path_;
    void*                base_     = nullptr;
    std::uint64_t        entry_    = 0;
    std::vector<Segment> segments_;
    bool                 parsed_   = false;
    std::string          lastError_;
    std::vector<char>    fileBuf_;  // Raw file bytes for dynamic table parsing
};

} // namespace kudroid