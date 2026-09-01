#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sys/mman.h>

#ifndef DT_INIT
#define DT_INIT 12
#endif
#ifndef DT_FINI
#define DT_FINI 13
#endif
#ifndef DT_INIT_ARRAY
#define DT_INIT_ARRAY 25
#endif
#ifndef DT_FINI_ARRAY
#define DT_FINI_ARRAY 26
#endif
#ifndef DT_INIT_ARRAYSZ
#define DT_INIT_ARRAYSZ 27
#endif
#ifndef DT_FINI_ARRAYSZ
#define DT_FINI_ARRAYSZ 28
#endif
// DT_RELR (packed relative relocations, NDK r23+ default for arm64)
#ifndef DT_RELR
#define DT_RELR 36
#endif
#ifndef DT_RELRSZ
#define DT_RELRSZ 35
#endif
#ifndef DT_RELRENT
#define DT_RELRENT 37
#endif
#ifndef DT_HASH
#define DT_HASH 4
#endif
#ifndef DT_GNU_HASH
#define DT_GNU_HASH 0x6ffffef5
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <libkern/OSCacheControl.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <pthread.h>
#endif
#endif

namespace kudroid {

// elf64 header structure
#pragma pack(push, 1)
struct Elf64Ehdr {
    uint8_t  e_ident[16];   // magic + class/endian/v.v.
    uint16_t e_type;        // et_dyn, et_exec, v.v.
    uint16_t e_machine;     // em_aarch64 = 0xb7
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;       // offset of the beginning of the program
    uint64_t e_shoff;       // displacement of the beginning of the segment
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;   // size of each program header item
    uint16_t e_phnum;       // number of program header items
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64Phdr {
    uint32_t p_type;    // pt_load=1, pt_dynamic=2, v.v.
    uint32_t p_flags;   // pf_r=4, pf_w=2, pf_x=1
    uint64_t p_offset;  // file displacement
    uint64_t p_vaddr;   // virtual address
    uint64_t p_paddr;   // physical address (not used)
    uint64_t p_filesz;  // size in file
    uint64_t p_memsz;   // size in memory
    uint64_t p_align;   // margin
};
#pragma pack(pop)

static const uint8_t ELF_MAGIC[4] = {0x7f, 'E', 'L', 'F'};
static const uint16_t EM_AARCH64 = 0xB7;
static const uint16_t EM_X86_64   = 0x3E;

static const char* machine_name(uint16_t machine) {
    switch (machine) {
        case EM_AARCH64: return "AArch64";
        case EM_X86_64:  return "x86-64";
        default:         return "unknown";
    }
}

ElfLoader::ElfLoader(std::string path)
    : path_(std::move(path)) {}

ElfLoader::~ElfLoader() {
    releaseFileMapping();
    if (allocBase_) {
        ::munmap(allocBase_, allocSize_);
    }
}

void ElfLoader::releaseFileMapping() {
    if (fileData_ == nullptr) return;
    // Build the symbol index first if nobody has: releasing the file without it would
    // make every later getSymbolAddress fail, and the failure would look like a
    // missing symbol rather than a missing table.
    if (!symbolIndexBuilt_) buildSymbolIndex();
    ::munmap(const_cast<char*>(fileData_), fileSize_);
    fileData_ = nullptr;
    fileSize_ = 0;
}

ElfLoader::ElfLoader(ElfLoader&& other) noexcept
    : path_(std::move(other.path_)),
      base_(other.base_),
      allocBase_(other.allocBase_),
      allocSize_(other.allocSize_),
      entry_(other.entry_),
      segments_(std::move(other.segments_)),
      parsed_(other.parsed_),
      lastError_(std::move(other.lastError_)),
      fileData_(other.fileData_),
      fileSize_(other.fileSize_),
      symbolIndex_(std::move(other.symbolIndex_)),
      symbolIndexBuilt_(other.symbolIndexBuilt_),
      libraryManager_(other.libraryManager_),
      tls_vaddr_(other.tls_vaddr_),
      tls_filesz_(other.tls_filesz_),
      tls_memsz_(other.tls_memsz_),
      tls_align_(other.tls_align_),
      init_func_(other.init_func_),
      init_array_(other.init_array_),
      init_arraysz_(other.init_arraysz_),
      fini_func_(other.fini_func_),
      fini_array_(other.fini_array_),
      fini_arraysz_(other.fini_arraysz_),
      eh_frame_vaddr_(other.eh_frame_vaddr_),
      eh_frame_memsz_(other.eh_frame_memsz_) {
    // Transfer ownership of mmap region; The source cannot be munmaped again.
    other.allocBase_ = nullptr;
    other.allocSize_ = 0;
    other.base_ = nullptr;
}

ElfLoader& ElfLoader::operator=(ElfLoader&& other) noexcept {
    if (this != &other) {
        releaseFileMapping();
        if (allocBase_) {
            ::munmap(allocBase_, allocSize_);
        }
        path_ = std::move(other.path_);
        base_ = other.base_;
        allocBase_ = other.allocBase_;
        allocSize_ = other.allocSize_;
        entry_ = other.entry_;
        segments_ = std::move(other.segments_);
        parsed_ = other.parsed_;
        lastError_ = std::move(other.lastError_);
        fileData_ = other.fileData_;
        fileSize_ = other.fileSize_;
        symbolIndex_ = std::move(other.symbolIndex_);
        symbolIndexBuilt_ = other.symbolIndexBuilt_;
        libraryManager_ = other.libraryManager_;
        tls_vaddr_ = other.tls_vaddr_;
        tls_filesz_ = other.tls_filesz_;
        tls_memsz_ = other.tls_memsz_;
        tls_align_ = other.tls_align_;
        init_func_ = other.init_func_;
        init_array_ = other.init_array_;
        init_arraysz_ = other.init_arraysz_;
        fini_func_ = other.fini_func_;
        fini_array_ = other.fini_array_;
        fini_arraysz_ = other.fini_arraysz_;
        eh_frame_vaddr_ = other.eh_frame_vaddr_;
        eh_frame_memsz_ = other.eh_frame_memsz_;
        other.allocBase_ = nullptr;
        other.allocSize_ = 0;
        other.base_ = nullptr;
        other.fileData_ = nullptr;
        other.fileSize_ = 0;
    }
    return *this;
}

const char* ElfLoader::lastError() const {
    return lastError_.c_str();
}

bool ElfLoader::parse() {
    segments_.clear();
    entry_ = 0;
    lastError_.clear();

    // open file
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        lastError_ = "Cannot open file: " + path_;
        return false;
    }

    // Read the first part elf
    Elf64Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!file) {
        lastError_ = "Failed to read ELF header";
        return false;
    }

    // authentic magic elf
    if (memcmp(ehdr.e_ident, ELF_MAGIC, 4) != 0) {
        lastError_ = "Not an ELF file (bad magic)";
        return false;
    }

    // must be 64-bit
    if (ehdr.e_ident[4] != 2) {  // elfclass64
        lastError_ = "Not a 64-bit ELF";
        return false;
    }

    // must be little-endian
    if (ehdr.e_ident[5] != 1) {  // elfdata2lsb
        lastError_ = "Not little-endian ELF";
        return false;
    }

    // must be aarch64 or x86-64
    if (ehdr.e_machine != EM_AARCH64 && ehdr.e_machine != EM_X86_64) {
        char mbuf[64];
        snprintf(mbuf, sizeof(mbuf), "Unsupported machine: %s (0x%x)",
                 machine_name(ehdr.e_machine), ehdr.e_machine);
        lastError_ = mbuf;
        return false;
    }

    // must be a shared object (et_dyn=3) or executable (et_exec=2)
    if (ehdr.e_type != 2 && ehdr.e_type != 3) {
        lastError_ = "ELF type not ET_EXEC or ET_DYN (type=" + std::to_string(ehdr.e_type) + ")";
        return false;
    }

    entry_ = ehdr.e_entry;

    // Verify the number of program headers
    if (ehdr.e_phnum == 0) {
        lastError_ = "No program headers";
        return false;
    }

    // The program header table's own location, for dl_iterate_phdr.
    //
    // e_phoff is a FILE offset, and what the guest unwinder needs is a mapped address. For
    // every NDK .so the table sits in the first PT_LOAD segment, which starts at file
    // offset 0 with p_vaddr == 0, so the file offset and the vaddr coincide. PT_PHDR states
    // the vaddr outright when present and is preferred below.
    phdr_vaddr_ = ehdr.e_phoff;
    phdr_count_ = ehdr.e_phnum;

    // Read the first parts of the program
    file.seekg(ehdr.e_phoff);
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        Elf64Phdr phdr;
        file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (!file) {
            lastError_ = "Failed to read program header " + std::to_string(i);
            return false;
        }

        // pt_load and pt_tls segments only
        if (phdr.p_type == 1) {  // pt_load
            Segment seg;
            seg.vaddr  = phdr.p_vaddr;
            seg.offset = phdr.p_offset;
            seg.filesz = phdr.p_filesz;
            seg.memsz  = phdr.p_memsz;
            seg.flags  = phdr.p_flags;
            segments_.push_back(seg);
        } else if (phdr.p_type == 7) { // pt_tls
            // tls initialization template
            tls_vaddr_ = phdr.p_vaddr;
            tls_filesz_ = phdr.p_filesz;
            tls_memsz_  = phdr.p_memsz;
            tls_align_  = phdr.p_align;
        } else if (phdr.p_type == 0x6474e550) { // pt_gnu_eh_frame
            eh_frame_vaddr_ = phdr.p_vaddr;
            eh_frame_memsz_ = phdr.p_memsz;
        } else if (phdr.p_type == 6) { // pt_phdr — the table's own mapped address
            phdr_vaddr_ = phdr.p_vaddr;
        }
    }

    parsed_ = true;
    return true;
}

// dynamic segmentation item
struct Elf64Dyn {
    int64_t  d_tag;       // dt_null, dt_symtab, v.v.
    uint64_t d_val;       // value (address/size)
};

// symbol table item
struct Elf64Sym {
    uint32_t st_name;     // displacement in .dynstr
    uint8_t  st_info;     // style + link
    uint8_t  st_other;    // visibility
    uint16_t st_shndx;    // segment index
    uint64_t st_value;    // symbolic value (displacement from origin)
    uint64_t st_size;     // symbol size
};

struct Elf64Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

// the dynamic tags we care about
static const int64_t DT_NULL   = 0;
static const int64_t DT_SYMTAB = 6;
static const int64_t DT_STRTAB = 5;
static const int64_t DT_STRSZ  = 10;

static const int64_t DT_RELA   = 7;
static const int64_t DT_RELASZ = 8;
static const int64_t DT_RELAENT = 9;
static const int64_t DT_JMPREL = 23;
static const int64_t DT_PLTRELSZ = 2;

static const uint32_t R_AARCH64_RELATIVE = 1027;
static const uint32_t R_AARCH64_GLOB_DAT = 1025;
static const uint32_t R_AARCH64_JUMP_SLOT = 1026;
static const uint32_t R_AARCH64_ABS64 = 257;
static const uint32_t R_AARCH64_IRELATIVE = 1032;

// Count the number of REAL symbols in .dynsym using hash table (DT_HASH nchain or
// DT_GNU_HASH chains), instead of the distance (strtabOff - symtabOff)/24.
//
// .dynstr is NOT right after .dynsym in .so NDK — there is between them
// .gnu.hash/.hash, so the distance for the number is too large → the wrong junk entry is scanned
// (st_name happens to point to the correct string, st_shndx != 0) → getSymbolAddress returns
// base+st_value of GARBAGE → GOT slot points to the ELF → SIGILL header area when called.
// It's the Discord case: "__cxa_atexit resolved from libreactnative.so ->
// 0x112ab5080" (= base+0x1080, pc crash).
static size_t countDynsymSymbols(const char* fileData, size_t fileSize,
                                 const Elf64Ehdr* ehdr,
                                 const Elf64Phdr* phdrs) {
    const Elf64Dyn* dynamic = nullptr;
    size_t dynCount = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == 2 &&
            phdrs[i].p_offset + phdrs[i].p_filesz <= fileSize) {
            dynamic = reinterpret_cast<const Elf64Dyn*>(
                fileData + phdrs[i].p_offset);
            dynCount = phdrs[i].p_filesz / sizeof(Elf64Dyn);
            break;
        }
    }
    if (!dynamic) return 0;

    auto vaddrToOffset = [&](uint64_t vaddr) -> uint64_t {
        for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            if (phdrs[i].p_type == 1 && vaddr >= phdrs[i].p_vaddr &&
                vaddr < phdrs[i].p_vaddr + phdrs[i].p_filesz) {
                return phdrs[i].p_offset + (vaddr - phdrs[i].p_vaddr);
            }
        }
        return UINT64_MAX;
    };

    uint64_t hashOff = UINT64_MAX;      // DT_HASH (sysv)
    uint64_t gnuHashOff = UINT64_MAX;   // DT_GNU_HASH
    for (size_t i = 0; i < dynCount && dynamic[i].d_tag != 0; ++i) {
        if (dynamic[i].d_tag == DT_HASH) {
            hashOff = vaddrToOffset(dynamic[i].d_val);
        } else if (dynamic[i].d_tag == DT_GNU_HASH) {
            gnuHashOff = vaddrToOffset(dynamic[i].d_val);
        }
    }

    // DT_HASH: { nbucket, nchain, buckets[], chains[] } — nchain = number of symbols.
    if (hashOff != UINT64_MAX && hashOff + 8 <= fileSize) {
        const uint32_t* h =
            reinterpret_cast<const uint32_t*>(fileData + hashOff);
        const uint32_t nchain = h[1];
        if (nchain > 0 && nchain < 10000000) return nchain;
    }

    // DT_GNU_HASH: { nbuckets, symoffset, bloom_size, bloom_shift, bloom[],
    // buckets[], chains[] }. Number of symbols = symoffset + max(chain index). Walk
    // each chain (bit 0 = last entry) to find the largest index.
    if (gnuHashOff != UINT64_MAX && gnuHashOff + 16 <= fileSize) {
        const uint32_t* g =
            reinterpret_cast<const uint32_t*>(fileData + gnuHashOff);
        const uint32_t nbuckets = g[0];
        const uint32_t symoffset = g[1];
        const uint32_t bloomSize = g[2];
        if (nbuckets < 1000000 && symoffset < 10000000) {
            uint64_t pos = gnuHashOff + 16;
            pos += (uint64_t)bloomSize * 8; // bloom words (64-bit)
            if (pos + (uint64_t)nbuckets * 4 <= fileSize) {
                const uint32_t* buckets =
                    reinterpret_cast<const uint32_t*>(fileData + pos);
                pos += (uint64_t)nbuckets * 4;
                uint32_t maxIdx = 0;
                for (uint32_t b = 0; b < nbuckets; ++b) {
                    uint32_t idx = buckets[b];
                    if (idx < symoffset) continue;
                    for (;;) {
                        const uint64_t off =
                            pos + (uint64_t)(idx - symoffset) * 4;
                        if (off + 4 > fileSize) break;
                        const uint32_t chain = *reinterpret_cast<const uint32_t*>(
                            fileData + off);
                        if (idx > maxIdx) maxIdx = idx;
                        if (chain & 1) break;
                        ++idx;
                    }
                }
                if (maxIdx > 0) return (size_t)maxIdx + 1;
            }
        }
    }

    return 0; // no hash table → caller fallback on distance
}

bool ElfLoader::mapFile() {
    if (fileData_ != nullptr) return true;

    const int fd = ::open(path_.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st = {};
    if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        return false;
    }
    const size_t size = static_cast<size_t>(st.st_size);
    // PROT_READ MAP_PRIVATE: the pages stay CLEAN, so the kernel can evict them under
    // memory pressure and read them back from disk. A heap copy (which this replaced)
    // is dirty and can only be swapped — which iOS does not do, so it counted in full
    // against the process footprint. libminecraftpe.so is 333 MB; the copy plus the
    // loaded image put the process over 660 MB before a single frame was drawn, and
    // jetsam killed it with SIGKILL.
    void* mapped = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) return false;

    fileData_ = static_cast<const char*>(mapped);
    fileSize_ = size;
    return true;
}

// Copy .dynsym into a hash map so symbol lookup outlives the file mapping.
//
// Also a large speed win independent of memory: getSymbolAddress used to re-parse
// PT_DYNAMIC and then linear-scan the whole symbol table on EVERY call, and symbol
// resolution runs once per relocation — hundreds of thousands of times for a game this
// size.
void ElfLoader::buildSymbolIndex() {
    symbolIndexBuilt_ = true;
    if (fileData_ == nullptr || fileSize_ < sizeof(Elf64Ehdr)) return;

    const auto* ehdr = reinterpret_cast<const Elf64Ehdr*>(fileData_);
    if (ehdr->e_phnum == 0) return;
    if (ehdr->e_phoff + static_cast<uint64_t>(ehdr->e_phnum) * sizeof(Elf64Phdr) >
        fileSize_) {
        return;
    }
    const auto* phdrs = reinterpret_cast<const Elf64Phdr*>(fileData_ + ehdr->e_phoff);

    const Elf64Dyn* dynamic = nullptr;
    size_t dynCount = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == 2 && phdrs[i].p_offset + phdrs[i].p_filesz <= fileSize_) {
            dynamic = reinterpret_cast<const Elf64Dyn*>(fileData_ + phdrs[i].p_offset);
            dynCount = phdrs[i].p_filesz / sizeof(Elf64Dyn);
            break;
        }
    }
    if (dynamic == nullptr) return;

    auto vaddrToOffset = [&](uint64_t vaddr) -> uint64_t {
        for (uint16_t j = 0; j < ehdr->e_phnum; ++j) {
            if (phdrs[j].p_type == 1 && vaddr >= phdrs[j].p_vaddr &&
                vaddr < phdrs[j].p_vaddr + phdrs[j].p_filesz) {
                return phdrs[j].p_offset + (vaddr - phdrs[j].p_vaddr);
            }
        }
        return UINT64_MAX;
    };

    const Elf64Sym* symtab = nullptr;
    const char* strtab = nullptr;
    size_t strsz = 0;
    uint64_t symtabOff = UINT64_MAX;
    uint64_t strtabOff = UINT64_MAX;
    for (size_t i = 0; i < dynCount && dynamic[i].d_tag != DT_NULL; ++i) {
        switch (dynamic[i].d_tag) {
            case DT_SYMTAB: {
                const uint64_t off = vaddrToOffset(dynamic[i].d_val);
                if (off != UINT64_MAX && off < fileSize_) {
                    symtab = reinterpret_cast<const Elf64Sym*>(fileData_ + off);
                    symtabOff = off;
                }
                break;
            }
            case DT_STRTAB: {
                const uint64_t off = vaddrToOffset(dynamic[i].d_val);
                if (off != UINT64_MAX && off < fileSize_) {
                    strtab = fileData_ + off;
                    strtabOff = off;
                }
                break;
            }
            case DT_STRSZ:
                strsz = dynamic[i].d_val;
                break;
            default:
                break;
        }
    }
    if (symtab == nullptr || strtab == nullptr) return;

    // The real count comes from the hash table, never from the symtab->strtab
    // distance: .dynstr does not follow .dynsym directly (.gnu.hash sits between), so
    // the distance overcounts and the scan matches junk entries whose st_name happens
    // to land on a valid string. That produced base+st_value of garbage — the Discord
    // SIGILL at base+0x1080.
    size_t maxSym = countDynsymSymbols(fileData_, fileSize_, ehdr, phdrs);
    if (maxSym == 0) {
        if (strtabOff != UINT64_MAX && strtabOff > symtabOff) {
            maxSym = (strtabOff - symtabOff) / sizeof(Elf64Sym);
        } else {
            maxSym = (fileSize_ - symtabOff) / sizeof(Elf64Sym);
        }
    }
    if (symtabOff + maxSym * sizeof(Elf64Sym) > fileSize_) {
        maxSym = (fileSize_ - symtabOff) / sizeof(Elf64Sym);
    }

    symbolIndex_.reserve(maxSym);
    for (size_t i = 0; i < maxSym; ++i) {
        if (symtab[i].st_name == 0) continue;
        if (strsz > 0 && symtab[i].st_name >= strsz) continue;
        // st_shndx == 0 is an UNDEFINED symbol — this library imports it rather than
        // exporting it, so resolving it here would hand out base+st_value of nothing.
        if (symtab[i].st_shndx == 0) continue;
        const char* name = strtab + symtab[i].st_name;
        if (name[0] == '\0') continue;
        // First definition wins, matching the previous linear scan's behaviour.
        symbolIndex_.emplace(name, symtab[i].st_value);
    }
    std::fprintf(stderr, "[KuDroidELF] symbol index for %s: %zu exports\n",
                 path_.c_str(), symbolIndex_.size());
}

bool ElfLoader::map() {
    if (!parsed_) {
        lastError_ = "Must call parse() before map()";
        return false;
    }

    if (segments_.empty()) {
        lastError_ = "No PT_LOAD segments to map";
        return false;
    }

    // Map the file read-only. Clean pages, so nothing here counts as dirty footprint.
    if (!mapFile()) {
        lastError_ = "Failed to map file: " + std::string(strerror(errno));
        return false;
    }

    // find total memory size needed (from lowest to highest virtual address+memsz)
    uint64_t minVaddr = UINT64_MAX;
    uint64_t maxVaddr = 0;
    for (const auto& seg : segments_) {
        if (seg.vaddr < minVaddr) minVaddr = seg.vaddr;
        uint64_t end = seg.vaddr + seg.memsz;
        if (end > maxVaddr) maxVaddr = end;
    }
    const long pageSizeValue = sysconf(_SC_PAGESIZE);
    if (pageSizeValue <= 0) {
        lastError_ = "Failed to determine page size";
        return false;
    }
    const uint64_t pageSize = static_cast<uint64_t>(pageSizeValue);
    uint64_t totalSize = maxVaddr - minVaddr;
    totalSize = (totalSize + pageSize - 1) & ~(pageSize - 1);

    // allocate a contiguous block of memory (with map_jit on apple silicon)
    fprintf(stderr, "[KuDroidELF] Mapping ELF segments for %s (size: %zu)\n", path_.c_str(), (size_t)totalSize);
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    [[maybe_unused]] bool usedMapJit = false;
#if defined(__APPLE__) && TARGET_OS_OSX
    // preferred path (hardened runtime): map_jit pages, written only during
    // The stream's jit write protection is disabled.
    base_ = mmap(nullptr, totalSize, prot, flags | MAP_JIT, -1, 0);
    if (base_ != MAP_FAILED) {
        usedMapJit = true;
    } else {
        base_ = mmap(nullptr, totalSize, prot, flags, -1, 0);
    }
#else
    base_ = mmap(nullptr, totalSize, prot, flags, -1, 0);
#endif
    if (base_ == MAP_FAILED) {
        lastError_ = "mmap failed (no JIT?): " + std::string(strerror(errno));
        base_ = nullptr;
        return false;
    }
    // Save the initial allocated region base to the safe munmap destructor (base_ device
    // adjusted backwards by -minVaddr at the end of this function).
    allocBase_ = base_;
    allocSize_ = totalSize;

#if defined(__APPLE__) && TARGET_OS_OSX
    if (usedMapJit) pthread_jit_write_protect_np(0);
#endif

    // copies the data of each pt_load segment from the file buffer into mapped memory
    for (const auto& seg : segments_) {
        fprintf(stderr, "[KuDroidELF] Segment: vaddr=0x%llx, offset=0x%llx, filesz=0x%llx, memsz=0x%llx, flags=%d\n",
                (unsigned long long)seg.vaddr, (unsigned long long)seg.offset,
                (unsigned long long)seg.filesz, (unsigned long long)seg.memsz, seg.flags);
        char* dst = static_cast<char*>(base_) + (seg.vaddr - minVaddr);
        if (seg.offset + seg.filesz <= fileSize_) {
            memcpy(dst, fileData_ + seg.offset, seg.filesz);
        }
        // fill .bss with zeros (memsz > filesz)
        if (seg.memsz > seg.filesz) {
            memset(dst + seg.filesz, 0, seg.memsz - seg.filesz);
        }
    }

    // --- aot patcher for tpidr_el0 (kudroid root class) ---
    // ios xnu kernel doesn't context switch for tpidr_el0, so it returns garbage.
    // mrs xn, tpidr_el0 -> 0xd53bd040 | n
    // we patch it to brk #(0x1000 + n) -> 0xd4200000 | ((0x1000 + n) << 5)
    //
    // IMPORTANT: this loop must run BEFORE pthread_jit_write_protect_np(1)
    // enabled again (macOS MAP_JIT) — otherwise, writing to the JIT page is failing
    // lock will cause fault. Relocking is performed immediately after the loop.
    for (const auto& seg : segments_) {
        if (seg.flags & 1) { // PROT_EXEC
            uint32_t* insts = reinterpret_cast<uint32_t*>(static_cast<char*>(base_) + (seg.vaddr - minVaddr));
            size_t num_insts = seg.filesz / 4;
            for (size_t i = 0; i < num_insts; i++) {
                uint32_t inst = insts[i];
                if ((inst & 0xFFFFFFE0) == 0xD53BD040) {
                    uint32_t reg = inst & 0x1F;
                    uint32_t brk_inst = 0xD4200000 | ((0x1000 + reg) << 5);
                    insts[i] = brk_inst;
                }
            }
        }
    }

    // Adjust base_ to point to logical address 0
    // (let base_ + st_value = real address)
    base_ = static_cast<char*>(base_) - minVaddr;

    // Register the module's TLS template to the runtime's per-thread TLS blocks
    // can copy it to the corresponding tprel location (see kudroid_tls_module_offset).
    if (tls_vaddr_ != 0 && tls_filesz_ > 0 && tls_filesz_ <= tls_memsz_) {
        kudroid_tls_set_template(static_cast<char*>(base_) + tls_vaddr_, tls_filesz_);
    }

    // Register the guest module (base_..base_+minVaddr+totalSize) to crash handler
    // symbolicate is pc located in this .so — dladdr does not know region due
    // ELF loader mmap should have previously printed "no symbol".
    kudroid_register_guest_module(base_, minVaddr + totalSize, path_.c_str());

    // Register the program headers so the guest's own unwinder can find its exception
    // tables. A statically linked libc++abi — which is what every NDK app ships — locates
    // PT_GNU_EH_FRAME through dl_iterate_phdr and nothing else. Without this, throwing
    // inside a guest library finds no FDE and calls std::terminate instead of unwinding.
    if (phdr_count_ != 0) {
        kudroid_register_guest_phdrs(base_, static_cast<char*>(base_) + phdr_vaddr_,
                                     phdr_count_);
    }

    return true;
}

bool ElfLoader::relocate() {
    if (!parsed_) {
        lastError_ = "Must call parse() before relocate()";
        return false;
    }
    if (!base_) {
        lastError_ = "Must call map() before relocate()";
        return false;
    }
#if defined(__APPLE__) && TARGET_OS_OSX
    // map() enabled again pthread_jit_write_protect_np(1) (MAP_JIT) — write to zone
    // The mapping will now fault. Relocate writes addend/symbol to .got/.data in
    // JIT area, so write-protect must be temporarily disabled during this process.
    pthread_jit_write_protect_np(0);
#endif
    const auto* ehdr = reinterpret_cast<const Elf64Ehdr*>(fileData_);
    const auto* phdrs = reinterpret_cast<const Elf64Phdr*>(
        fileData_ + ehdr->e_phoff);
    const Elf64Dyn* dynamic = nullptr;
    size_t dynamicCount = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == 2 &&
            phdrs[i].p_offset + phdrs[i].p_filesz <= fileSize_) {
            dynamic = reinterpret_cast<const Elf64Dyn*>(
                fileData_ + phdrs[i].p_offset);
            dynamicCount = phdrs[i].p_filesz / sizeof(Elf64Dyn);
            break;
        }
    }
    if (!dynamic) {
        lastError_ = "Missing PT_DYNAMIC";
        return false;
    }

    uint64_t relaVaddr = 0, relaSize = 0, relaEnt = sizeof(Elf64Rela);
    uint64_t jmpRelVaddr = 0, jmpRelSize = 0;
    uint64_t relrVaddr = 0, relrSize = 0;
    uint64_t symtabVaddr = 0, strtabVaddr = 0, strsz = 0;
    for (size_t i = 0; i < dynamicCount && dynamic[i].d_tag != DT_NULL; ++i) {
        switch (dynamic[i].d_tag) {
            case DT_RELA: relaVaddr = dynamic[i].d_val; break;
            case DT_RELASZ: relaSize = dynamic[i].d_val; break;
            case DT_RELAENT: relaEnt = dynamic[i].d_val; break;
            case DT_JMPREL: jmpRelVaddr = dynamic[i].d_val; break;
            case DT_PLTRELSZ: jmpRelSize = dynamic[i].d_val; break;
            case DT_RELR: relrVaddr = dynamic[i].d_val; break;
            case DT_RELRSZ: relrSize = dynamic[i].d_val; break;
            case DT_SYMTAB: symtabVaddr = dynamic[i].d_val; break;
            case DT_STRTAB: strtabVaddr = dynamic[i].d_val; break;
            case DT_STRSZ: strsz = dynamic[i].d_val; break;
            case DT_INIT: init_func_ = dynamic[i].d_val; break;
            case DT_INIT_ARRAY: init_array_ = dynamic[i].d_val; break;
            case DT_INIT_ARRAYSZ: init_arraysz_ = dynamic[i].d_val; break;
            case DT_FINI: fini_func_ = dynamic[i].d_val; break;
            case DT_FINI_ARRAY: fini_array_ = dynamic[i].d_val; break;
            case DT_FINI_ARRAYSZ: fini_arraysz_ = dynamic[i].d_val; break;
            default: break;
        }
    }

    auto vaddrToFileOffset = [&](uint64_t vaddr) -> uint64_t {
        for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
            const auto& phdr = phdrs[i];
            if (phdr.p_type == 1 && vaddr >= phdr.p_vaddr &&
                vaddr - phdr.p_vaddr < phdr.p_filesz) {
                return phdr.p_offset + (vaddr - phdr.p_vaddr);
            }
        }
        return UINT64_MAX;
    };

    const uint64_t symtabOffset = vaddrToFileOffset(symtabVaddr);
    const uint64_t strtabOffset = vaddrToFileOffset(strtabVaddr);
    if (symtabOffset == UINT64_MAX || strtabOffset == UINT64_MAX ||
        (strsz > 0 && strsz > fileSize_ - strtabOffset)) {
        lastError_ = "Invalid dynamic symbol tables";
        return false;
    }
    const auto* symtab = reinterpret_cast<const Elf64Sym*>(
        fileData_ + symtabOffset);
    const char* strtab = fileData_ + strtabOffset;
    // TRUE number of symbols from hash table — distance symtab→strtab is too large when
    // .gnu.hash/.hash is between → valid symbolIndex is still outside this limit
    // or considered trash (similar to bug getSymbolAddress → SIGILL Discord).
    size_t symbolCount = countDynsymSymbols(fileData_, fileSize_, ehdr, phdrs);
    if (symbolCount == 0) {
        symbolCount = (strtabOffset - symtabOffset) / sizeof(Elf64Sym);
    }

#ifndef R_AARCH64_COPY
#define R_AARCH64_COPY 1024
#endif
#ifndef R_AARCH64_TLS_DTPMOD64
#define R_AARCH64_TLS_DTPMOD64 1028
#endif
#ifndef R_AARCH64_TLS_DTPREL64
#define R_AARCH64_TLS_DTPREL64 1029
#endif
#ifndef R_AARCH64_TLS_TPREL64
#define R_AARCH64_TLS_TPREL64 1030
#endif

    auto applyRelocations = [&](uint64_t vaddr, uint64_t size) -> bool {
        if (size == 0) return true;
        const uint64_t offset = vaddrToFileOffset(vaddr);
        if (offset == UINT64_MAX || relaEnt != sizeof(Elf64Rela) ||
            size % relaEnt != 0 || size > fileSize_ - offset) {
            lastError_ = "Invalid relocation table";
            return false;
        }
        const auto* relocs = reinterpret_cast<const Elf64Rela*>(
            fileData_ + offset);
        for (uint64_t i = 0; i < size / relaEnt; ++i) {
            const uint32_t type = static_cast<uint32_t>(relocs[i].r_info & 0xffffffffu);
            const uint32_t symbolIndex = static_cast<uint32_t>(relocs[i].r_info >> 32);
            auto* target = reinterpret_cast<uint64_t*>(
                static_cast<char*>(base_) + relocs[i].r_offset);
            if (type == R_AARCH64_RELATIVE) {
                *target = reinterpret_cast<uintptr_t>(base_) + relocs[i].r_addend;
            } else if (type == R_AARCH64_IRELATIVE) {
                typedef uintptr_t (*ifunc_resolver_t)(void);
                auto resolver = reinterpret_cast<ifunc_resolver_t>(
                    static_cast<char*>(base_) + relocs[i].r_addend);
                *target = resolver ? resolver() : 0;
            } else if (type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_ABS64 || type == R_AARCH64_COPY) {
                if (symbolIndex >= symbolCount || symtab[symbolIndex].st_name >= strsz) {
                    lastError_ = "Invalid relocation symbol index";
                    return false;
                }
                const char* name = strtab + symtab[symbolIndex].st_name;
                void* address = nullptr;
                // Weak undefined (STB_WEAK + SHN_UNDEF): Android linker STILL
                // bind to the strong symbol if it exists in the loaded libs
                // (e.g. strlcpy/strlcat is in bionic libc). Only if NOT looking
                // If you see it anywhere, keep 0 — the caller code always has `cbz x8, skip`
                // ahead to handle null branches (feature detection). Bind hard
                // nullptr previously caused slot GOT = 0 even though shim HAS that function → profit
                // call via PLT jump to pc=0x0 → SIGSEGV fault_addr=0x0 exactly as
                // crash Minecraft/libmaesdk (register x0/x1/x2 matches strlcpy).
                const bool isWeakUndef = (symtab[symbolIndex].st_shndx == 0) &&
                                         ((symtab[symbolIndex].st_info >> 4) == 2);
                if (symtab[symbolIndex].st_shndx != 0) {
                    address = static_cast<char*>(base_) + symtab[symbolIndex].st_value;
                } else if (libraryManager_) {
                    address = libraryManager_->resolveGlobalSymbol(name);
                    if (!address) address = resolve_bionic_symbol(name);
                } else {
                    address = resolve_bionic_symbol(name);
                }
                // isWeakUndef is now only used to silence the warning when the symbol is real
                // does not exist — the correct behavior of linker: weak undef does not exist
                // Wherever slot = 0, the caller automatically checks for null.
                
                if (!address && !isWeakUndef) {
                    // All symbols cannot be resolved — not just STB_GLOBAL — because of GOT
                    // slot at that time = 0 + addend (possibly a junk address in the data),
                    // Guest calls through this slot → SIGILL/SIGSEGV (suspect in Discord case).
                    // Print type + r_offset to know exactly which slot in the module is broken.
                    fprintf(stderr,
                            "[KuDroidELF] WARNING: Unresolved symbol '%s' in %s (type=%u, offset=0x%llx, addend=0x%llx)\n",
                            name, path_.c_str(), type,
                            (unsigned long long)relocs[i].r_offset,
                            (unsigned long long)relocs[i].r_addend);
                }
                
                if (type == R_AARCH64_COPY) {
                    if (address) {
                        memcpy(target, address, symtab[symbolIndex].st_size);
                    }
                } else {
                    // IMPORTANT: If address == nullptr (weak symbol not found),
                    // assign true 0 (NULL). Do not add addend to NULL because it will create an address
                    // Small garbage (eg 0x18/0x20) makes the app think the pointer is valid and then crashes SIGSEGV.
                    *target = address ? (reinterpret_cast<uintptr_t>(address) + relocs[i].r_addend) : 0;
                }
            } else if (type == R_AARCH64_TLS_DTPMOD64) {
                *target = 1; // module id (1 for main library)
            } else if (type == R_AARCH64_TLS_DTPREL64 || type == R_AARCH64_TLS_TPREL64) {
                // Bias = position of the module's TLS template relative to the guest thread pointer
                // (this value is set by kudroid_tls_set_template in the TLS block).
                const uint64_t tlsBias = kudroid_tls_module_offset();
                if (symbolIndex != 0) {
                    *target = symtab[symbolIndex].st_value + relocs[i].r_addend + tlsBias;
                } else {
                    *target = relocs[i].r_addend + tlsBias;
                }
            } else {
                lastError_ = "Unsupported AArch64 relocation type: " +
                             std::to_string(type);
                return false;
            }
        }
        return true;
    };

    // DT_RELR — packed relative relocations (NDK r23+ default for arm64).
    //
    // Format: each entry 8 bytes.
    //  - bit 0 = 0: entry is a relative address that needs reloc (r_offset).
    //  - bit 0 = 1: bitmap — BASE address taken from previous entry (single address);
    //    Bits 1..63 mark consecutive addresses (base + bit*8) that need reloc.
    //    Consecutive bitmap: next base = base + 63*8.
    //
    // Unlike RELA, the addend of RELR is AVAILABLE in the file content at the reloc location
    // (not in the entry), so the way to apply is to ADD the load bias to the current value
    // yes — exactly like glibc/musl/Android linker (*(addr) += load_bias).
    auto applyRelr = [&]() -> bool {
        if (relrVaddr == 0 || relrSize == 0) return true;
        const uint64_t offset = vaddrToFileOffset(relrVaddr);
        if (offset == UINT64_MAX || relrSize > fileSize_ - offset || relrSize % 8 != 0) {
            lastError_ = "Invalid RELR table";
            return false;
        }
        const auto* relrs = reinterpret_cast<const uint64_t*>(fileData_ + offset);
        const uintptr_t bias = reinterpret_cast<uintptr_t>(base_);
        uint64_t lastAddr = 0;
        for (uint64_t i = 0; i < relrSize / 8; ++i) {
            const uint64_t entry = relrs[i];
            // The first entry of the RELR table MUST be a single entry (bit 0 = 0) as a base
            // for bitmaps — the spec does not define bitmaps in the beginning position.
            if (i == 0 && (entry & 1ULL)) {
                lastError_ = "Invalid RELR table: first entry is a bitmap";
                return false;
            }
            if (entry & 1ULL) {
                const uint64_t base = lastAddr; // base = address of previous single entry
                for (uint64_t bit = 1; bit < 64; ++bit) {
                    if (entry & (1ULL << bit)) {
                        const uint64_t vaddr = base + bit * 8;
                        *reinterpret_cast<uint64_t*>(static_cast<char*>(base_) + vaddr) += bias;
                    }
                }
                lastAddr = base + 63 * 8;
            } else {
                const uint64_t vaddr = entry;
                *reinterpret_cast<uint64_t*>(static_cast<char*>(base_) + vaddr) += bias;
                lastAddr = vaddr;
            }
        }
        return true;
    };

    const bool ok = applyRelocations(relaVaddr, relaSize) &&
                    applyRelocations(jmpRelVaddr, jmpRelSize) &&
                    applyRelr();
    fprintf(stderr,
            "[KuDroidELF] relocate(%s): RELA=%llu entries, JMPREL=%llu, RELR=%llu bytes, symtab=%s\n",
            path_.c_str(),
            static_cast<unsigned long long>(relaSize / relaEnt),
            static_cast<unsigned long long>(relaEnt ? jmpRelSize / relaEnt : 0),
            static_cast<unsigned long long>(relrSize),
            symtabVaddr != 0 ? "present" : "absent");

    if (ok) {
        applyProtections();
    }

#if defined(__APPLE__) && TARGET_OS_OSX
    pthread_jit_write_protect_np(1);
#endif
    return ok;
}

bool ElfLoader::applyProtections() {
    if (!allocBase_ || allocSize_ == 0) return true;

    const long pageSizeValue = sysconf(_SC_PAGESIZE);
    if (pageSizeValue <= 0) return false;
    const uint64_t pageSize = static_cast<uint64_t>(pageSizeValue);

    uint64_t minVaddr = UINT64_MAX;
    for (const auto& seg : segments_) {
        if (seg.vaddr < minVaddr) minVaddr = seg.vaddr;
    }
    if (minVaddr == UINT64_MAX) minVaddr = 0;

    const size_t numPages = allocSize_ / pageSize;
    std::vector<int> pageProts(numPages, 0);

    for (const auto& seg : segments_) {
        const uint64_t segmentStart = seg.vaddr - minVaddr;
        const uint64_t segmentEnd = segmentStart + seg.memsz;
        
        int segmentProt = 0;
        if (seg.flags & 4) segmentProt |= PROT_READ;
        if (seg.flags & 2) segmentProt |= PROT_WRITE;
        if (seg.flags & 1) segmentProt |= PROT_EXEC;

        size_t startPage = segmentStart / pageSize;
        size_t endPage = (segmentEnd + pageSize - 1) / pageSize;
        for (size_t i = startPage; i < endPage && i < numPages; ++i) {
            pageProts[i] |= segmentProt;
        }
    }

    // On iOS (and non-JIT environments), pages marked both writable and executable
    // (W^X violation) result in SIGBUS when instructions are fetched from them.
    // Since all ELF relocations, GOT updates, and initial data writes have ALREADY
    // been performed into the mapping while it was writable, strip PROT_WRITE from
    // any page that contains executable code (PROT_EXEC) so that it remains cleanly RX.
    for (size_t i = 0; i < numPages; ++i) {
        if (pageProts[i] & PROT_EXEC) {
            pageProts[i] &= ~PROT_WRITE;
            pageProts[i] |= (PROT_READ | PROT_EXEC);
        }
    }

    char* mapStart = static_cast<char*>(allocBase_);
    size_t groupStart = 0;
    while (groupStart < numPages) {
        int currentProt = pageProts[groupStart];
        size_t groupEnd = groupStart + 1;
        while (groupEnd < numPages && pageProts[groupEnd] == currentProt) {
            groupEnd++;
        }

        if (currentProt != 0) {
            if (mprotect(mapStart + (groupStart * pageSize),
                         (groupEnd - groupStart) * pageSize, currentProt) != 0) {
                lastError_ = "mprotect failed (no JIT?): " +
                             std::string(strerror(errno));
                return false;
            }
        }
        groupStart = groupEnd;
    }

#if defined(__APPLE__)
    sys_icache_invalidate(mapStart, allocSize_);
#else
    __builtin___clear_cache(mapStart, mapStart + allocSize_);
#endif

    return true;
}

void* ElfLoader::getSymbolAddress(const char* symbolName) {
    if (!base_ || symbolName == nullptr || *symbolName == '\0') {
        return nullptr;
    }
    // Answered from the index built at load time, not by re-parsing the file.
    //
    // This used to re-read PT_DYNAMIC and then linear-scan .dynsym on every call, which
    // is why it required the file to still be mapped and why resolution was O(symbols)
    // per relocation — hundreds of thousands of scans over ~40k symbols for a game the
    // size of Minecraft.
    if (!symbolIndexBuilt_) buildSymbolIndex();
    const auto it = symbolIndex_.find(symbolName);
    if (it == symbolIndex_.end()) return nullptr;
    // st_value is an offset from the load origin; base_ was adjusted by -minVaddr.
    return static_cast<char*>(base_) + it->second;
}

std::string ElfLoader::testExecution() {
    if (!base_) {
        return "[kudroid_core] EXECUTION FAILED: Library not loaded (call map first)";
    }

    void* addr = getSymbolAddress("kudroid_add");
    if (!addr) {
        return "[kudroid_core] EXECUTION FAILED: Symbol 'kudroid_add' not found";
    }

    // signature: int kudroid_add(int, int)
    int (*add_func)(int, int) = reinterpret_cast<int (*)(int, int)>(addr);
    int result = add_func(40, 20);

    return "[kudroid_core] EXECUTION SUCCESS: kudroid_add(40, 20) = " +
           std::to_string(result);
}

void ElfLoader::executeInit() {
    if (!base_) return;

    // execute dt_init if present
    if (init_func_ != 0) {
        void (*init)() = reinterpret_cast<void (*)()>(
            static_cast<char*>(base_) + init_func_);
        init();
    }

    // execute dt_init_array if present
    if (init_array_ != 0 && init_arraysz_ > 0) {
        auto** array = reinterpret_cast<void (**)()>(
            static_cast<char*>(base_) + init_array_);
        size_t count = init_arraysz_ / sizeof(void*);
        for (size_t i = 0; i < count; ++i) {
            void (*func)() = array[i];
            if (func != nullptr && func != reinterpret_cast<void(*)()>(-1) && func != reinterpret_cast<void(*)()>(0xffffffffffffffffULL)) {
                func();
            }
        }
    }
}

void ElfLoader::executeFini() {
    if (!base_) return;

    // execute dt_fini_array if present (in reverse order according to elf specification)
    if (fini_array_ != 0 && fini_arraysz_ > 0) {
        auto** array = reinterpret_cast<void (**)()>(
            static_cast<char*>(base_) + fini_array_);
        size_t count = fini_arraysz_ / sizeof(void*);
        for (size_t i = count; i > 0; --i) {
            void (*func)() = array[i - 1];
            if (func != nullptr && func != reinterpret_cast<void(*)()>(-1) && func != reinterpret_cast<void(*)()>(0xffffffffffffffffULL)) {
                func();
            }
        }
    }

    // execute dt_fini if ​​present
    if (fini_func_ != 0) {
        void (*fini)() = reinterpret_cast<void (*)()>(
            static_cast<char*>(base_) + fini_func_);
        fini();
    }
}

extern "C" void __register_frame(void*);
extern "C" void __deregister_frame(void*);

void ElfLoader::registerEhFrame() {
    if (!base_ || eh_frame_vaddr_ == 0) return;

    // eh_frame_vaddr_ points to .eh_frame_hdr (pt_gnu_eh_frame)
    auto* hdr = reinterpret_cast<const uint8_t*>(static_cast<char*>(base_) + eh_frame_vaddr_);
    // Bounds check: read minimum 8 bytes (version+3 enc + 4 byte pointer).
    if (eh_frame_memsz_ < 8) return;
    
    // header format:
    // uint8_t version; (must be 1)
    // uint8_t eh_frame_ptr_enc;
    // uint8_t fde_count_enc;
    // uint8_t table_enc;
    
    if (hdr[0] != 1) return; // phi n b n kh ng x c  nh
    
    // dw_eh_pe_pcrel | dw_eh_pe_sdata4 (0x1b) is the most common
    if (hdr[1] == 0x1B) {
        // pointer is a 32-bit signed offset from the pointer's address
        const int32_t* ptr_addr = reinterpret_cast<const int32_t*>(hdr + 4);
        int32_t offset = *ptr_addr;
        void* eh_frame_actual = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(ptr_addr) + offset);
        
        __register_frame(eh_frame_actual);
    }
}

void ElfLoader::deregisterEhFrame() {
    if (!base_ || eh_frame_vaddr_ == 0) return;

    auto* hdr = reinterpret_cast<const uint8_t*>(static_cast<char*>(base_) + eh_frame_vaddr_);
    if (eh_frame_memsz_ < 8) return;
    if (hdr[0] != 1) return;
    
    if (hdr[1] == 0x1B) {
        const int32_t* ptr_addr = reinterpret_cast<const int32_t*>(hdr + 4);
        int32_t offset = *ptr_addr;
        void* eh_frame_actual = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(ptr_addr) + offset);
        
        __deregister_frame(eh_frame_actual);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Registry module guest — crash handler symbolicate.
//
// C c region do ELF loader mmap kh ng n m trong dyld image list n n dladdr
// don't know; This registry maps base → (size, path) to the crash handler to print
// "<path>+0x<offset>" thay v  "(no symbol)".
//
// Ghi x y ra l c load ( n lu ng kh i  ng, c  lock   an to n n u 2 lu ng
// load at the same time). Read occurs in crash handler (can be on any thread)
// and NO lock — avoid deadlock if crash occurs while another thread is running
// gi  mutex. Sau khi load xong registry g n nh  b t bi n n n  c kh ng lock
// l  ch p nh n  c (best effort nh  m i ph n kh c c a crash handler).
namespace {
struct GuestModule {
    std::uintptr_t base;
    std::size_t    size;
    std::string    path;
    // The mapped program header table, for dl_iterate_phdr. Zero until the module's
    // headers are registered, which happens right after mapping.
    const void*    phdrs = nullptr;
    unsigned short phnum = 0;
};
std::mutex g_guestMtx;
std::vector<GuestModule> g_guestModules;
} // namespace

extern "C" void kudroid_register_guest_module(void* base, std::size_t size,
                                              const char* path) {
    if (!base || size == 0 || !path) return;
    std::lock_guard<std::mutex> lock(g_guestMtx);
    const auto addr = reinterpret_cast<std::uintptr_t>(base);
    for (auto& m : g_guestModules) {
        if (m.base == addr) {  // reload c ng module: c p nh t thay v  th m tr ng
            m.size = size;
            m.path = path;
            return;
        }
    }
    g_guestModules.push_back({addr, size, std::string(path)});
}

extern "C" void kudroid_register_guest_phdrs(void* base, const void* phdrs,
                                            unsigned short phnum) {
    if (!base || !phdrs || phnum == 0) return;
    std::lock_guard<std::mutex> lock(g_guestMtx);
    const auto addr = reinterpret_cast<std::uintptr_t>(base);
    for (auto& m : g_guestModules) {
        if (m.base == addr) {
            m.phdrs = phdrs;
            m.phnum = phnum;
            return;
        }
    }
    // Headers before the module: only possible if the caller registers out of order, which
    // the loader does not, but recording them is better than dropping them.
    GuestModule module{addr, 0, std::string(), phdrs, phnum};
    g_guestModules.push_back(std::move(module));
}

// bionic's dl_phdr_info, as a guest compiled against <link.h> expects it.
//
// Only the first four fields are read by any unwinder, but the struct must be the right
// SIZE too: the callback receives it and bionic passes sizeof(dl_phdr_info), so a short
// struct would have the guest read past the end of ours.
namespace {
struct GuestDlPhdrInfo {
    std::uintptr_t   dlpi_addr;      // load bias — what to add to a p_vaddr
    const char*      dlpi_name;
    const void*      dlpi_phdr;
    unsigned short   dlpi_phnum;
    // Present in bionic since API 21 and part of the struct's size even when unused.
    unsigned long long dlpi_adds;
    unsigned long long dlpi_subs;
    std::size_t      dlpi_tls_modid;
    void*            dlpi_tls_data;
};
} // namespace

extern "C" int kudroid_iterate_guest_phdrs(
    int (*callback)(void* info, std::size_t size, void* data), void* data) {
    if (!callback) return 0;

    // A snapshot under the lock, then the callback outside it. The guest's unwinder can
    // throw from inside the callback, and holding the mutex across that would deadlock the
    // next load — as well as any nested dl_iterate_phdr the unwinder chooses to make.
    std::vector<GuestModule> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_guestMtx);
        snapshot = g_guestModules;
    }

    for (const auto& m : snapshot) {
        if (!m.phdrs || m.phnum == 0) continue;
        GuestDlPhdrInfo info{};
        // base_ was already adjusted to the logical origin (see map()), so a p_vaddr plus
        // this base is the final address — which is exactly what dlpi_addr means.
        info.dlpi_addr = m.base;
        info.dlpi_name = m.path.c_str();
        info.dlpi_phdr = m.phdrs;
        info.dlpi_phnum = m.phnum;
        const int r = callback(&info, sizeof(info), data);
        // Non-zero stops the walk and is the callback's result, per the contract. The
        // unwinder returns non-zero the moment it finds the module holding the PC.
        if (r != 0) return r;
    }
    return 0;
}

extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize) {
    if (!addr || !out || outSize == 0) return false;
    const auto a = reinterpret_cast<std::uintptr_t>(addr);
    // c kh ng lock (xem ghi ch  ph a tr n)   vector ch  b  s a l c load.
    for (const auto& m : g_guestModules) {
        if (a >= m.base && a < m.base + m.size) {
            const auto off = a - m.base;
            const int n = snprintf(out, outSize, "0x%llx %s+0x%llx",
                                   (unsigned long long)a, m.path.c_str(),
                                   (unsigned long long)off);
            return n > 0 && static_cast<std::size_t>(n) < outSize;
        }
    }
    return false;
}

} // namespace kudroid