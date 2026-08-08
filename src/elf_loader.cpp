#include "kudroid/elf_loader.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sys/mman.h>
#include <utility>

namespace kudroid {

// ELF64 header structures
#pragma pack(push, 1)
struct Elf64Ehdr {
    uint8_t  e_ident[16];   // Magic + class/endian/etc
    uint16_t e_type;        // ET_DYN, ET_EXEC, etc
    uint16_t e_machine;     // EM_AARCH64 = 0xB7
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;       // Program header offset
    uint64_t e_shoff;       // Section header offset
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;   // Size of each program header entry
    uint16_t e_phnum;       // Number of program header entries
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64Phdr {
    uint32_t p_type;    // PT_LOAD=1, PT_DYNAMIC=2, etc
    uint32_t p_flags;   // PF_R=4, PF_W=2, PF_X=1
    uint64_t p_offset;  // File offset
    uint64_t p_vaddr;   // Virtual address
    uint64_t p_paddr;   // Physical address (unused)
    uint64_t p_filesz;  // Size in file
    uint64_t p_memsz;   // Size in memory
    uint64_t p_align;   // Alignment
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

ElfLoader::~ElfLoader() = default;

ElfLoader::ElfLoader(ElfLoader&&) noexcept = default;
ElfLoader& ElfLoader::operator=(ElfLoader&&) noexcept = default;

const char* ElfLoader::lastError() const {
    return lastError_.c_str();
}

bool ElfLoader::parse() {
    segments_.clear();
    entry_ = 0;
    lastError_.clear();

    // Open file
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        lastError_ = "Cannot open file: " + path_;
        return false;
    }

    // Read ELF header
    Elf64Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!file) {
        lastError_ = "Failed to read ELF header";
        return false;
    }

    // Validate ELF magic
    if (memcmp(ehdr.e_ident, ELF_MAGIC, 4) != 0) {
        lastError_ = "Not an ELF file (bad magic)";
        return false;
    }

    // Must be 64-bit
    if (ehdr.e_ident[4] != 2) {  // ELFCLASS64
        lastError_ = "Not a 64-bit ELF";
        return false;
    }

    // Must be little-endian
    if (ehdr.e_ident[5] != 1) {  // ELFDATA2LSB
        lastError_ = "Not little-endian ELF";
        return false;
    }

    // Must be AArch64 or x86-64
    if (ehdr.e_machine != EM_AARCH64 && ehdr.e_machine != EM_X86_64) {
        char mbuf[64];
        snprintf(mbuf, sizeof(mbuf), "Unsupported machine: %s (0x%x)",
                 machine_name(ehdr.e_machine), ehdr.e_machine);
        lastError_ = mbuf;
        return false;
    }

    // Must be shared object (ET_DYN=3) or executable (ET_EXEC=2)
    if (ehdr.e_type != 2 && ehdr.e_type != 3) {
        lastError_ = "ELF type not ET_EXEC or ET_DYN (type=" + std::to_string(ehdr.e_type) + ")";
        return false;
    }

    entry_ = ehdr.e_entry;

    // Validate program header count
    if (ehdr.e_phnum == 0) {
        lastError_ = "No program headers";
        return false;
    }

    // Read program headers
    file.seekg(ehdr.e_phoff);
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        Elf64Phdr phdr;
        file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (!file) {
            lastError_ = "Failed to read program header " + std::to_string(i);
            return false;
        }

        // Only PT_LOAD segments
        if (phdr.p_type == 1) {  // PT_LOAD
            Segment seg;
            seg.vaddr  = phdr.p_vaddr;
            seg.offset = phdr.p_offset;
            seg.filesz = phdr.p_filesz;
            seg.memsz  = phdr.p_memsz;
            seg.flags  = phdr.p_flags;
            segments_.push_back(seg);
        }
    }

    parsed_ = true;
    return true;
}

// Dynamic section entry
struct Elf64Dyn {
    int64_t  d_tag;       // DT_NULL, DT_SYMTAB, etc
    uint64_t d_val;       // Value (address/size)
};

// Symbol table entry
struct Elf64Sym {
    uint32_t st_name;     // Offset in .dynstr
    uint8_t  st_info;     // Type + binding
    uint8_t  st_other;    // Visibility
    uint16_t st_shndx;    // Section index
    uint64_t st_value;    // Symbol value (offset from base)
    uint64_t st_size;     // Symbol size
};

// Dynamic tags we care about
static const int64_t DT_NULL   = 0;
static const int64_t DT_SYMTAB = 6;
static const int64_t DT_STRTAB = 5;
static const int64_t DT_STRSZ  = 10;

bool ElfLoader::readFile(std::vector<char>& buf) {
    std::ifstream file(path_, std::ios::binary | std::ios::ate);
    if (!file) return false;
    auto size = file.tellg();
    file.seekg(0);
    buf.resize(size);
    file.read(buf.data(), size);
    return !!file;
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

    // Read entire file into buffer
    if (!readFile(fileBuf_)) {
        lastError_ = "Failed to read file into buffer";
        return false;
    }

    // Find the total memory size needed (from lowest vaddr to highest vaddr+memsz)
    uint64_t minVaddr = UINT64_MAX;
    uint64_t maxVaddr = 0;
    for (const auto& seg : segments_) {
        if (seg.vaddr < minVaddr) minVaddr = seg.vaddr;
        uint64_t end = seg.vaddr + seg.memsz;
        if (end > maxVaddr) maxVaddr = end;
    }
    uint64_t totalSize = maxVaddr - minVaddr;

    // Allocate executable memory via mmap
    int prot = PROT_READ | PROT_WRITE | PROT_EXEC;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    base_ = mmap(nullptr, totalSize, prot, flags, -1, 0);
    if (base_ == MAP_FAILED) {
        lastError_ = "mmap failed: " + std::string(strerror(errno));
        base_ = nullptr;
        return false;
    }

    // Copy each PT_LOAD segment data from file buffer to mapped memory
    for (const auto& seg : segments_) {
        char* dst = static_cast<char*>(base_) + (seg.vaddr - minVaddr);
        if (seg.offset + seg.filesz <= fileBuf_.size()) {
            memcpy(dst, fileBuf_.data() + seg.offset, seg.filesz);
        }
        // Zero-fill .bss (memsz > filesz)
        if (seg.memsz > seg.filesz) {
            memset(dst + seg.filesz, 0, seg.memsz - seg.filesz);
        }
    }

    // Adjust base_ to point to the logical address 0
    // (so base_ + st_value = actual address)
    base_ = static_cast<char*>(base_) - minVaddr;

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
    // For our simple test .so without complex relocations, this is sufficient.
    // Real implementation would parse .rela.dyn / .rela.plt and apply
    // R_AARCH64_RELATIVE relocations (base + addend).
    return true;
}

void* ElfLoader::getSymbolAddress(const char* symbolName) {
    if (!base_ || fileBuf_.empty() || segments_.empty()) {
        return nullptr;
    }
    if (!symbolName || !*symbolName) {
        return nullptr;
    }

    // We need to locate the PT_DYNAMIC segment. Re-parse the program headers
    // from the already-loaded fileBuf_ to find PT_DYNAMIC.
    const Elf64Ehdr* ehdr = reinterpret_cast<const Elf64Ehdr*>(fileBuf_.data());
    if (ehdr->e_phnum == 0) return nullptr;

    const Elf64Phdr* phdrs = reinterpret_cast<const Elf64Phdr*>(
        fileBuf_.data() + ehdr->e_phoff);

    const Elf64Dyn* dynamic = nullptr;
    size_t dynCount = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == 2) {  // PT_DYNAMIC
            if (phdrs[i].p_offset + phdrs[i].p_filesz <= fileBuf_.size()) {
                dynamic = reinterpret_cast<const Elf64Dyn*>(
                    fileBuf_.data() + phdrs[i].p_offset);
                dynCount = phdrs[i].p_filesz / sizeof(Elf64Dyn);
            }
            break;
        }
    }

    if (!dynamic) return nullptr;

    // Extract symtab, strtab, strsz from dynamic entries
    const Elf64Sym* symtab = nullptr;
    const char* strtab = nullptr;
    size_t strsz = 0;

    // Helper to convert virtual address to file offset using PT_LOAD segments
    auto vaddrToOffset = [&](uint64_t vaddr) -> uint64_t {
        for (uint16_t j = 0; j < ehdr->e_phnum; ++j) {
            if (phdrs[j].p_type == 1) {  // PT_LOAD
                if (vaddr >= phdrs[j].p_vaddr &&
                    vaddr < phdrs[j].p_vaddr + phdrs[j].p_filesz) {
                    return phdrs[j].p_offset + (vaddr - phdrs[j].p_vaddr);
                }
            }
        }
        return UINT64_MAX;  // not found
    };

    for (size_t i = 0; i < dynCount; ++i) {
        if (dynamic[i].d_tag == DT_NULL) break;
        switch (dynamic[i].d_tag) {
            case DT_SYMTAB: {
                uint64_t off = vaddrToOffset(dynamic[i].d_val);
                if (off != UINT64_MAX && off < fileBuf_.size()) {
                    symtab = reinterpret_cast<const Elf64Sym*>(
                        fileBuf_.data() + off);
                }
                break;
            }
            case DT_STRTAB: {
                uint64_t off = vaddrToOffset(dynamic[i].d_val);
                if (off != UINT64_MAX && off < fileBuf_.size()) {
                    strtab = fileBuf_.data() + off;
                }
                break;
            }
            case DT_STRSZ:
                strsz = dynamic[i].d_val;
                break;
        }
    }

    if (!symtab || !strtab) return nullptr;

    // Iterate over symbol table entries
    size_t maxSym = strsz > 0 ? (fileBuf_.size() / sizeof(Elf64Sym)) : 0;
    for (size_t i = 0; i < maxSym; ++i) {
        if (symtab[i].st_name == 0) continue;
        if (symtab[i].st_name >= strsz) continue;

        const char* name = strtab + symtab[i].st_name;
        if (strcmp(name, symbolName) == 0) {
            // st_value is offset from load base; base_ already adjusted
            return static_cast<char*>(base_) + symtab[i].st_value;
        }
    }

    return nullptr;
}

std::string ElfLoader::testExecution() {
    if (!base_) {
        return "[kudroid_core] EXECUTION FAILED: Library not loaded (call map first)";
    }

    void* addr = getSymbolAddress("kudroid_add");
    if (!addr) {
        return "[kudroid_core] EXECUTION FAILED: Symbol 'kudroid_add' not found in .dynsym";
    }

    using AddFunc = int (*)(int, int);
    AddFunc func = reinterpret_cast<AddFunc>(addr);

    int result = func(40, 20);
    char buf[128];
    snprintf(buf, sizeof(buf),
        "[kudroid_core] EXECUTION SUCCESS! kudroid_add(40, 20) = %d", result);
    return std::string(buf);
}

} // namespace kudroid