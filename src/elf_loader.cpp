#include "kudroid/elf_loader.hpp"

#include <cstring>
#include <fstream>
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

bool ElfLoader::map() {
    // TODO(Phase1): mmap PT_LOAD segments with correct permissions.
    if (!parsed_) {
        lastError_ = "Must call parse() before map()";
        return false;
    }
    // Stub: return true for now, actual mmap implementation later
    return true;
}

bool ElfLoader::relocate() {
    // TODO(Phase1): Process R_AARCH64_* relocations and bind symbols.
    if (!parsed_) {
        lastError_ = "Must call parse() before relocate()";
        return false;
    }
    // Stub: return true for now
    return true;
}

} // namespace kudroid