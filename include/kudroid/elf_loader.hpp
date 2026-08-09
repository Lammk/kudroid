#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace kudroid {

class LibraryManager;

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

    void setLibraryManager(LibraryManager* manager) { libraryManager_ = manager; }

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
    LibraryManager*      libraryManager_ = nullptr;
};

} // namespace kudroid

namespace kudroid {

/// Return DT_NEEDED library names from an ELF64 shared object.
std::vector<std::string> parse_elf_dependencies(const char* elf_path);

/// Extract lib/arm64-v8a/*.so entries from an APK into outputDirectory.
bool extract_arm64_libs_from_apk(const char* apkPath, const char* outputDirectory,
                                 std::string* error = nullptr);

class LibraryManager {
public:
    /// Load an ELF and all DT_NEEDED dependencies from its directory.
    bool loadRecursive(const std::string& path);
    /// Return a symbol from any loaded ELF, then BionicShim as fallback.
    void* resolveGlobalSymbol(const char* name) const;
    [[nodiscard]] const std::unordered_map<std::string, std::unique_ptr<ElfLoader>>& libraries() const {
        return libraries_;
    }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    std::unordered_map<std::string, std::unique_ptr<ElfLoader>> libraries_;
    std::string lastError_;
};

} // namespace kudroid