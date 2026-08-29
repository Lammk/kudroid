#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>

namespace kudroid {

/// register the mapped elf guest module (base/size/path) to crash handler
///symbolicate is pc located in guest .so (dladdr doesn't know region due to
///ELF loader mmap should have previously printed "no symbol"). Safety called multiple times.
extern "C" void kudroid_register_guest_module(void* base, std::size_t size,
                                              const char* path);

/// find the guest module containing the address addr; write "path+0x<offset>" to out if found.
/// returns true if found. Used in crash handler (read only, not locked).
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

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
    // internal: read file content into buffer
    bool readFile(std::vector<char>& buf);

    std::string          path_;
    void*                base_     = nullptr;
    // The base of the original mmap region (before base_ is adjusted by -minVaddr),
    // destructor c  th  munmap an to n.
    void*                allocBase_ = nullptr;
    std::size_t          allocSize_ = 0;
    std::uint64_t        entry_    = 0;
    std::vector<Segment> segments_;
    bool                 parsed_   = false;
    std::string          lastError_;
    std::vector<char>    fileBuf_;  // byte t p th    ph n t ch b ng  ng
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
};

} // namespace kudroid

namespace kudroid {

// / tr  v  t n c c th  vi n dt_needed t  m t  i t ng chia s  elf64.
std::vector<std::string> parse_elf_dependencies(const char* elf_path);

// / tr ch xu t c c m c lib/arm64-v8a/*.so t  m t t p apk v o outputdirectory.
bool extract_arm64_libs_from_apk(const char* apkPath, const char* outputDirectory,
                                 std::string* error = nullptr);

class LibraryManager {
public:
    // / t i m t elf v  t t c  c c ph  thu c dt_needed t  th  m c c a n .
    bool loadRecursive(const std::string& path);
    // / tr  v  m t k  hi u t  b t k  elf n o  c t i, sau   bionicshim l m d  ph ng.
    void* resolveGlobalSymbol(const char* name) const;
    // / tr  v  m t k  hi u c  th  t  th  vi n  ng d ng ch nh (th  t   n  nh).
    void* resolveAppSymbol(const char* name);
    // / tr  v  k  hi u t  M I elf  c t i (  s p x p theo t n    n  nh)
    // / Android g i JNI_OnLoad cho t ng th  vi n  c loadLibrary, n n runner
    // / ph i g i cho t t c  th  vi n c  export, kh ng ch  lib  u ti n t y  .
    std::vector<std::pair<std::string, void*>> resolveAllSymbols(const char* name) const;
    void* resolveSymbolInLib(const std::string& libPattern, const char* name) const;
    [[nodiscard]] const std::unordered_map<std::string, std::unique_ptr<ElfLoader>>& libraries() const {
        return libraries_;
    }
    [[nodiscard]] const std::string& lastError() const { return lastError_; }

private:
    // Sorted view of libraries_, rebuilt only when the set of libraries changes.
    //
    // Symbol resolution is called once per relocation, which for a game the size of
    // Minecraft is hundreds of thousands of times. Sorting a fresh vector of eight
    // std::strings on every one of those calls is pure overhead on the startup path.
    const std::vector<std::string>& sortedKeys() const;
    void invalidateCaches();

    mutable std::recursive_mutex mtx_;
    std::unordered_map<std::string, std::unique_ptr<ElfLoader>> libraries_;
    std::string lastError_;

    mutable std::vector<std::string> sortedKeys_;
    mutable bool sortedKeysValid_ = false;

    // Resolved symbol addresses, including negative results.
    //
    // Repeated lookups of the same symbol are the norm, not the exception: one
    // startup produced 55748 identical resolutions of
    // _ZTVN10__cxxabiv120__si_class_type_infoE, each one a full scan of every
    // loaded ELF plus a log line. That was 30 MB of stderr in which the same five
    // lines accounted for 111k of 115k total, burying every real diagnostic.
    //
    // Caching a miss is as important as caching a hit — a symbol that is absent gets
    // asked for just as often and costs the same full scan. Both maps are dropped
    // whenever the library set changes, since a newly loaded ELF can turn a former
    // miss into a hit.
    mutable std::unordered_map<std::string, void*> globalSymbolCache_;
    mutable std::unordered_map<std::string, void*> appSymbolCache_;
};

} // namespace kudroid