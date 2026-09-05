#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>

namespace kudroid {

/// Register a mapped guest module so the crash handler can symbolicate guest PCs.
/// Safe to call multiple times.
extern "C" void kudroid_register_guest_module(void* base, std::size_t size,
                                              const char* path);

/// find the guest module containing the address addr; write "path+0x<offset>" to out if found.
/// returns true if found. Used in crash handler (read only, not locked).
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

/// Report a guest module's program headers, for dl_iterate_phdr.
/// Lets the guest unwinder find its EH frames; `phdrs` stays caller-owned.
extern "C" void kudroid_register_guest_phdrs(void* base, const void* phdrs,
                                            unsigned short phnum);

/// Walk the registered guest modules, matching the bionic dl_iterate_phdr contract.
/// Stops and returns the callback's value as soon as it returns non-zero.
extern "C" int kudroid_iterate_guest_phdrs(
    int (*callback)(void* info, std::size_t size, void* data), void* data);

class LibraryManager;

///minimum elf64 (arm64) loader surface.
///stage 1 entry point — analysis, mapping, and repositioning pseudofunctions.
class ElfLoader {
public:
    struct Segment {
        std::uint64_t vaddr  = 0;   ///< target virtual address
        std::uint64_t offset = 0;   ///< file offset of pt_load data
        std::uint64_t filesz = 0;   ///< number of bytes in file
        std::uint64_t memsz  = 0;   ///< number of bytes in memory (>= filesz)
        std::uint32_t flags  = 0;   ///< pf_r | pf_w | pf_x
    };

    explicit ElfLoader(std::string path);
    ~ElfLoader();

    // Cannot be copied, can be moved
    ElfLoader(const ElfLoader&) = delete;
    ElfLoader& operator=(const ElfLoader&) = delete;
    ElfLoader(ElfLoader&&) noexcept;
    ElfLoader& operator=(ElfLoader&&) noexcept;

    /// parses elf64 headers and populates the segment list.
    bool parse();

    /// maps pt_load segments to executable memory.
    bool map();

    /// applies dynamic repositioning and resolution of symbols.
    bool relocate();

    void setLibraryManager(LibraryManager* manager) { libraryManager_ = manager; }

    [[nodiscard]] std::uint64_t entryPoint() const { return entry_; }
    [[nodiscard]] const std::vector<Segment>& segments() const { return segments_; }
    [[nodiscard]] bool isLoaded() const { return base_ != nullptr; }
    [[nodiscard]] bool isParsed() const { return parsed_; }
    [[nodiscard]] void* baseAddress() const { return base_; }

    /// returns the final error message (empty if there is no error).
    [[nodiscard]] const char* lastError() const;

    ///phase 2: lookup symbol address from .dynsym
    /// @return function pointer (base_ + st_value), or nullptr if not found.
    void* getSymbolAddress(const char* symbolName);

    /// Release the mapping of the ELF FILE, keeping the loaded image.
    /// Only needed while parsing/relocating; holding it doubles the footprint.
    void releaseFileMapping();

    ///phase 2: test execution – call kudroid_add(40, 20) via dynamic notation.
    /// @return result string with state and calculated value.
    std::string testExecution();

    /// execute constructors (dt_init and dt_init_array).
    void executeInit();

    /// execute destructors (dt_fini and dt_fini_array).
    void executeFini();

    ///register .eh_frame for c++ exceptions
    void registerEhFrame();
    void deregisterEhFrame();

private:
    // Map the ELF file read-only instead of copying it into the heap.
    // A file mapping stays clean so the kernel can evict it under pressure.
    bool mapFile();

    // Build a name -> st_value index from .dynsym, so getSymbolAddress no longer needs
    // the file. Called once, before the file mapping is released.
    void buildSymbolIndex();

    // Apply final W^X safe segment protections after relocations have completed.
    bool applyProtections();

    std::string          path_;
    void*                base_     = nullptr;
    // The base of the original mmap region (before base_ is adjusted by -minVaddr),
    // so the destructor can munmap safely.
    void*                allocBase_ = nullptr;
    std::size_t          allocSize_ = 0;
    std::uint64_t        entry_    = 0;
    std::vector<Segment> segments_;
    bool                 parsed_   = false;
    std::string          lastError_;

    // The ELF file, mapped PROT_READ MAP_PRIVATE. Valid between mapFile() and
    // releaseFileMapping().
    const char*          fileData_ = nullptr;
    std::size_t          fileSize_ = 0;

    // Exported symbols, captured from .dynsym before the file mapping goes away.
    // Values are st_value (offsets from the load origin), not final addresses, so the
    // table stays valid regardless of what base_ ends up being.
    std::unordered_map<std::string, std::uint64_t> symbolIndex_;
    bool                 symbolIndexBuilt_ = false;

    LibraryManager*      libraryManager_ = nullptr;
    
    // tls
    std::uint64_t        tls_vaddr_  = 0;
    std::uint64_t        tls_filesz_ = 0;
    std::uint64_t        tls_memsz_  = 0;
    std::uint64_t        tls_align_  = 0;

    // constructor/destructor
    std::uint64_t        init_func_  = 0;
    std::uint64_t        init_array_ = 0;
    std::uint64_t        init_arraysz_ = 0;
    std::uint64_t        fini_func_  = 0;
    std::uint64_t        fini_array_ = 0;
    std::uint64_t        fini_arraysz_ = 0;

    // exception handling (.eh_frame_hdr / pt_gnu_eh_frame)
    std::uint64_t        eh_frame_vaddr_ = 0;
    std::uint64_t        eh_frame_memsz_ = 0;

    // The program header table's own vaddr and count, for dl_iterate_phdr.
    std::uint64_t        phdr_vaddr_ = 0;
    std::uint16_t        phdr_count_ = 0;
};

} // namespace kudroid

namespace kudroid {

// Return DT_NEEDED library names from an elf64 shared object.
std::vector<std::string> parse_elf_dependencies(const char* elf_path);

// Extract lib/arm64-v8a/*.so entries from an APK into the output directory.
bool extract_arm64_libs_from_apk(const char* apkPath, const char* outputDirectory,
                                 std::string* error = nullptr);

class LibraryManager {
public:
    // Load an ELF plus all its DT_NEEDED dependencies.
    bool loadRecursive(const std::string& path);
    // Resolve a symbol from any loaded ELF, falling back to the bionic shim.
    void* resolveGlobalSymbol(const char* name) const;
    // Resolve a symbol from the main app library.
    void* resolveAppSymbol(const char* name);
    // Return matches from every loaded ELF (sorted by name).
    // Android calls JNI_OnLoad per loadLibrary, so the runner must try all exporting libs.
    std::vector<std::pair<std::string, void*>> resolveAllSymbols(const char* name) const;
    void* resolveSymbolInLib(const std::string& libPattern, const char* name) const;
    [[nodiscard]] const std::unordered_map<std::string, std::unique_ptr<ElfLoader>>& libraries() const {
        return libraries_;
    }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    // Sorted view of libraries_, rebuilt only when the set changes.
    // Resolution runs per relocation, so re-sorting every call is overhead.
    const std::vector<std::string>& sortedKeys() const;
    void invalidateCaches();

    mutable std::recursive_mutex mtx_;
    std::unordered_map<std::string, std::unique_ptr<ElfLoader>> libraries_;
    std::string lastError_;

    mutable std::vector<std::string> sortedKeys_;
    mutable bool sortedKeysValid_ = false;

    // Resolved symbol addresses, including negative results.
    // Both maps are dropped when the library set changes.
    mutable std::unordered_map<std::string, void*> globalSymbolCache_;
    mutable std::unordered_map<std::string, void*> appSymbolCache_;
};

} // namespace kudroid