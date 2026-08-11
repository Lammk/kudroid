#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"

#include <cerrno>
#include <cstring>
#include <fstream>
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

// cấu trúc phần đầu elf64
#pragma pack(push, 1)
struct Elf64Ehdr {
    uint8_t  e_ident[16];   // magic + class/endian/v.v.
    uint16_t e_type;        // et_dyn, et_exec, v.v.
    uint16_t e_machine;     // em_aarch64 = 0xb7
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;       // độ dời phần đầu chương trình
    uint64_t e_shoff;       // độ dời phần đầu phân đoạn
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;   // kích thước mỗi mục phần đầu chương trình
    uint16_t e_phnum;       // số lượng mục phần đầu chương trình
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64Phdr {
    uint32_t p_type;    // pt_load=1, pt_dynamic=2, v.v.
    uint32_t p_flags;   // pf_r=4, pf_w=2, pf_x=1
    uint64_t p_offset;  // độ dời tệp
    uint64_t p_vaddr;   // địa chỉ ảo
    uint64_t p_paddr;   // địa chỉ vật lý (không dùng)
    uint64_t p_filesz;  // kích thước trong tệp
    uint64_t p_memsz;   // kích thước trong bộ nhớ
    uint64_t p_align;   // căn lề
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

    // mở tệp
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        lastError_ = "Cannot open file: " + path_;
        return false;
    }

    // đọc phần đầu elf
    Elf64Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (!file) {
        lastError_ = "Failed to read ELF header";
        return false;
    }

    // xác thực magic elf
    if (memcmp(ehdr.e_ident, ELF_MAGIC, 4) != 0) {
        lastError_ = "Not an ELF file (bad magic)";
        return false;
    }

    // phải là 64-bit
    if (ehdr.e_ident[4] != 2) {  // elfclass64
        lastError_ = "Not a 64-bit ELF";
        return false;
    }

    // phải là little-endian
    if (ehdr.e_ident[5] != 1) {  // elfdata2lsb
        lastError_ = "Not little-endian ELF";
        return false;
    }

    // phải là aarch64 hoặc x86-64
    if (ehdr.e_machine != EM_AARCH64 && ehdr.e_machine != EM_X86_64) {
        char mbuf[64];
        snprintf(mbuf, sizeof(mbuf), "Unsupported machine: %s (0x%x)",
                 machine_name(ehdr.e_machine), ehdr.e_machine);
        lastError_ = mbuf;
        return false;
    }

    // phải là đối tượng chia sẻ (et_dyn=3) hoặc tệp thực thi (et_exec=2)
    if (ehdr.e_type != 2 && ehdr.e_type != 3) {
        lastError_ = "ELF type not ET_EXEC or ET_DYN (type=" + std::to_string(ehdr.e_type) + ")";
        return false;
    }

    entry_ = ehdr.e_entry;

    // xác thực số lượng phần đầu chương trình
    if (ehdr.e_phnum == 0) {
        lastError_ = "No program headers";
        return false;
    }

    // đọc các phần đầu chương trình
    file.seekg(ehdr.e_phoff);
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i) {
        Elf64Phdr phdr;
        file.read(reinterpret_cast<char*>(&phdr), sizeof(phdr));
        if (!file) {
            lastError_ = "Failed to read program header " + std::to_string(i);
            return false;
        }

        // chỉ các phân đoạn pt_load và pt_tls
        if (phdr.p_type == 1) {  // pt_load
            Segment seg;
            seg.vaddr  = phdr.p_vaddr;
            seg.offset = phdr.p_offset;
            seg.filesz = phdr.p_filesz;
            seg.memsz  = phdr.p_memsz;
            seg.flags  = phdr.p_flags;
            segments_.push_back(seg);
        } else if (phdr.p_type == 7) { // pt_tls
            // khuôn mẫu khởi tạo tls
            tls_vaddr_ = phdr.p_vaddr;
            tls_filesz_ = phdr.p_filesz;
            tls_memsz_  = phdr.p_memsz;
            tls_align_  = phdr.p_align;
        } else if (phdr.p_type == 0x6474e550) { // pt_gnu_eh_frame
            eh_frame_vaddr_ = phdr.p_vaddr;
            eh_frame_memsz_ = phdr.p_memsz;
        }
    }

    parsed_ = true;
    return true;
}

// mục phân đoạn động
struct Elf64Dyn {
    int64_t  d_tag;       // dt_null, dt_symtab, v.v.
    uint64_t d_val;       // giá trị (địa chỉ/kích thước)
};

// mục bảng ký hiệu
struct Elf64Sym {
    uint32_t st_name;     // độ dời trong .dynstr
    uint8_t  st_info;     // kiểu + liên kết
    uint8_t  st_other;    // khả năng hiển thị
    uint16_t st_shndx;    // chỉ mục phân đoạn
    uint64_t st_value;    // giá trị ký hiệu (độ dời từ gốc)
    uint64_t st_size;     // kích thước ký hiệu
};

struct Elf64Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

// các thẻ động chúng ta quan tâm
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

    // đọc toàn bộ tệp vào bộ đệm
    if (!readFile(fileBuf_)) {
        lastError_ = "Failed to read file into buffer";
        return false;
    }

    // tìm tổng kích thước bộ nhớ cần thiết (từ địa chỉ ảo thấp nhất đến cao nhất+memsz)
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

    // ánh xạ cấp quyền ghi trước, sau đó áp dụng các quyền elf cuối cùng sau khi sao chép.
    // trên ios, mmap rwx có thể thành công nhưng vẫn bị lỗi sigbus khi tải lệnh;
    // việc chuyển từ rw -> rx là đường dẫn debugger-jit được hỗ trợ.
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    [[maybe_unused]] bool usedMapJit = false;
#if defined(__APPLE__) && TARGET_OS_OSX
    // đường dẫn ưu tiên (hardened runtime): các trang map_jit, chỉ được ghi trong khi
    // tính năng bảo vệ ghi jit của luồng được tắt.
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

#if defined(__APPLE__) && TARGET_OS_OSX
    if (usedMapJit) pthread_jit_write_protect_np(0);
#endif

    // sao chép dữ liệu của mỗi phân đoạn pt_load từ bộ đệm tệp vào bộ nhớ đã ánh xạ
    for (const auto& seg : segments_) {
        char* dst = static_cast<char*>(base_) + (seg.vaddr - minVaddr);
        if (seg.offset + seg.filesz <= fileBuf_.size()) {
            memcpy(dst, fileBuf_.data() + seg.offset, seg.filesz);
        }
        // điền số 0 vào .bss (memsz > filesz)
        if (seg.memsz > seg.filesz) {
            memset(dst + seg.filesz, 0, seg.memsz - seg.filesz);
        }
    }

#if defined(__APPLE__) && TARGET_OS_OSX
    if (usedMapJit) pthread_jit_write_protect_np(1);
#endif

    // --- trình vá aot cho tpidr_el0 (lớp gốc kudroid) ---
    // hạt nhân xnu của ios không chuyển đổi ngữ cảnh cho tpidr_el0, nên nó trả về rác.
    // mrs xn, tpidr_el0 -> 0xd53bd040 | n
    // chúng tôi vá nó thành brk #(0x1000 + n) -> 0xd4200000 | ((0x1000 + n) << 5)
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

    // arm64 có bộ đệm i/d riêng biệt: mã mới ghi phải được xóa khỏi
    // bộ đệm dữ liệu và bộ đệm lệnh cũ phải bị vô hiệu hóa, nếu không cpu
    // sẽ thực thi rác và quá trình sẽ gặp sự cố (sigill/sigbus) mà không có nhật ký.
    char* mapStart = static_cast<char*>(base_);
#if defined(__APPLE__)
    sys_icache_invalidate(mapStart, totalSize);
#else
    __builtin___clear_cache(mapStart, mapStart + totalSize);
#endif

    const size_t numPages = totalSize / pageSize;
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
                munmap(mapStart, totalSize);
                base_ = nullptr;
                return false;
            }
        }
        groupStart = groupEnd;
    }

    // điều chỉnh base_ để trỏ đến địa chỉ logic 0
    // (để base_ + st_value = địa chỉ thực)
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
    const auto* ehdr = reinterpret_cast<const Elf64Ehdr*>(fileBuf_.data());
    const auto* phdrs = reinterpret_cast<const Elf64Phdr*>(
        fileBuf_.data() + ehdr->e_phoff);
    const Elf64Dyn* dynamic = nullptr;
    size_t dynamicCount = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        if (phdrs[i].p_type == 2 &&
            phdrs[i].p_offset + phdrs[i].p_filesz <= fileBuf_.size()) {
            dynamic = reinterpret_cast<const Elf64Dyn*>(
                fileBuf_.data() + phdrs[i].p_offset);
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
    uint64_t symtabVaddr = 0, strtabVaddr = 0, strsz = 0;
    for (size_t i = 0; i < dynamicCount && dynamic[i].d_tag != DT_NULL; ++i) {
        switch (dynamic[i].d_tag) {
            case DT_RELA: relaVaddr = dynamic[i].d_val; break;
            case DT_RELASZ: relaSize = dynamic[i].d_val; break;
            case DT_RELAENT: relaEnt = dynamic[i].d_val; break;
            case DT_JMPREL: jmpRelVaddr = dynamic[i].d_val; break;
            case DT_PLTRELSZ: jmpRelSize = dynamic[i].d_val; break;
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
        strtabOffset <= symtabOffset || strsz > fileBuf_.size() - strtabOffset) {
        lastError_ = "Invalid dynamic symbol tables";
        return false;
    }
    const auto* symtab = reinterpret_cast<const Elf64Sym*>(
        fileBuf_.data() + symtabOffset);
    const char* strtab = fileBuf_.data() + strtabOffset;
    const size_t symbolCount = (strtabOffset - symtabOffset) / sizeof(Elf64Sym);

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
            size % relaEnt != 0 || size > fileBuf_.size() - offset) {
            lastError_ = "Invalid relocation table";
            return false;
        }
        const auto* relocs = reinterpret_cast<const Elf64Rela*>(
            fileBuf_.data() + offset);
        for (uint64_t i = 0; i < size / relaEnt; ++i) {
            const uint32_t type = static_cast<uint32_t>(relocs[i].r_info & 0xffffffffu);
            const uint32_t symbolIndex = static_cast<uint32_t>(relocs[i].r_info >> 32);
            auto* target = reinterpret_cast<uint64_t*>(
                static_cast<char*>(base_) + relocs[i].r_offset);
            if (type == R_AARCH64_RELATIVE) {
                *target = reinterpret_cast<uintptr_t>(base_) + relocs[i].r_addend;
            } else if (type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT || type == R_AARCH64_ABS64 || type == R_AARCH64_COPY) {
                if (symbolIndex >= symbolCount || symtab[symbolIndex].st_name >= strsz) {
                    lastError_ = "Invalid relocation symbol index";
                    return false;
                }
                const char* name = strtab + symtab[symbolIndex].st_name;
                void* address = nullptr;
                if (symtab[symbolIndex].st_shndx != 0) {
                    address = static_cast<char*>(base_) + symtab[symbolIndex].st_value;
                } else if (libraryManager_) {
                    address = libraryManager_->resolveGlobalSymbol(name);
                } else {
                    address = resolve_bionic_symbol(name);
                }
                
                if (type == R_AARCH64_COPY) {
                    if (address) {
                        memcpy(target, address, symtab[symbolIndex].st_size);
                    }
                } else {
                    *target = reinterpret_cast<uintptr_t>(address) + relocs[i].r_addend;
                }
            } else if (type == R_AARCH64_TLS_DTPMOD64) {
                *target = 1; // id mô-đun (1 cho thư viện chính)
            } else if (type == R_AARCH64_TLS_DTPREL64 || type == R_AARCH64_TLS_TPREL64) {
                // hiện tại, giải quyết độ dời tls trực tiếp. việc thiết lập khối tls đúng cách đòi hỏi quản lý thời gian chạy phức tạp hơn.
                if (symbolIndex != 0) {
                    *target = symtab[symbolIndex].st_value + relocs[i].r_addend;
                } else {
                    *target = relocs[i].r_addend;
                }
            } else {
                lastError_ = "Unsupported AArch64 relocation type: " +
                             std::to_string(type);
                return false;
            }
        }
        return true;
    };

    return applyRelocations(relaVaddr, relaSize) &&
           applyRelocations(jmpRelVaddr, jmpRelSize);
}

void* ElfLoader::getSymbolAddress(const char* symbolName) {
    if (!base_ || fileBuf_.empty() || segments_.empty()) {
        return nullptr;
    }
    if (!symbolName || !*symbolName) {
        return nullptr;
    }

    // chúng ta cần xác định vị trí của phân đoạn pt_dynamic. phân tích lại các phần đầu chương trình
    // từ filebuf_ đã được tải để tìm pt_dynamic.
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

    // trích xuất symtab, strtab, strsz từ các mục động
    const Elf64Sym* symtab = nullptr;
    const char* strtab = nullptr;
    size_t strsz = 0;
    uint64_t symtabOff = UINT64_MAX;
    uint64_t strtabOff = UINT64_MAX;

    // hàm hỗ trợ chuyển đổi địa chỉ ảo thành độ dời tệp bằng các phân đoạn pt_load
    auto vaddrToOffset = [&](uint64_t vaddr) -> uint64_t {
        for (uint16_t j = 0; j < ehdr->e_phnum; ++j) {
            if (phdrs[j].p_type == 1) {  // PT_LOAD
                if (vaddr >= phdrs[j].p_vaddr &&
                    vaddr < phdrs[j].p_vaddr + phdrs[j].p_filesz) {
                    return phdrs[j].p_offset + (vaddr - phdrs[j].p_vaddr);
                }
            }
        }
        return UINT64_MAX;  // không tìm thấy
    };

    for (size_t i = 0; i < dynCount; ++i) {
        if (dynamic[i].d_tag == DT_NULL) break;
        switch (dynamic[i].d_tag) {
            case DT_SYMTAB: {
                uint64_t off = vaddrToOffset(dynamic[i].d_val);
                if (off != UINT64_MAX && off < fileBuf_.size()) {
                    symtab = reinterpret_cast<const Elf64Sym*>(
                        fileBuf_.data() + off);
                    symtabOff = off;
                }
                break;
            }
            case DT_STRTAB: {
                uint64_t off = vaddrToOffset(dynamic[i].d_val);
                if (off != UINT64_MAX && off < fileBuf_.size()) {
                    strtab = fileBuf_.data() + off;
                    strtabOff = off;
                }
                break;
            }
            case DT_STRSZ:
                strsz = dynamic[i].d_val;
                break;
        }
    }

    if (!symtab || !strtab) return nullptr;

    // xác định số lượng mục .dynsym một cách an toàn. bảng chuỗi hầu như
    // luôn theo sau bảng ký hiệu ngay lập tức, vì vậy hãy giới hạn số lượng bằng khoảng cách đó;
    // nếu không hãy quay lại cuối bộ đệm tệp. nếu không có giới hạn này, vòng lặp
    // sẽ đọc qua bộ đệm tệp đã ánh xạ và gặp sự cố.
    size_t maxSym = 0;
    if (strtabOff != UINT64_MAX && strtabOff > symtabOff) {
        maxSym = (strtabOff - symtabOff) / sizeof(Elf64Sym);
    } else {
        maxSym = (fileBuf_.size() - symtabOff) / sizeof(Elf64Sym);
    }

    // lặp qua các mục bảng ký hiệu
    for (size_t i = 0; i < maxSym; ++i) {
        if (symtab[i].st_name == 0) continue;
        if (strsz > 0 && symtab[i].st_name >= strsz) continue;

        const char* name = strtab + symtab[i].st_name;
        if (strcmp(name, symbolName) == 0) {
            if (symtab[i].st_shndx == 0) {
                continue;
            }
            // st_value là độ dời từ gốc tải; base_ đã được điều chỉnh
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
        return "[kudroid_core] EXECUTION FAILED: Symbol 'kudroid_add' not found";
    }

    // chữ ký: int kudroid_add(int, int)
    int (*add_func)(int, int) = reinterpret_cast<int (*)(int, int)>(addr);
    int result = add_func(40, 20);

    return "[kudroid_core] EXECUTION SUCCESS: kudroid_add(40, 20) = " +
           std::to_string(result);
}

void ElfLoader::executeInit() {
    if (!base_) return;

    // thực thi dt_init nếu có
    if (init_func_ != 0) {
        void (*init)() = reinterpret_cast<void (*)()>(
            static_cast<char*>(base_) + init_func_);
        init();
    }

    // thực thi dt_init_array nếu có
    if (init_array_ != 0 && init_arraysz_ > 0) {
        auto** array = reinterpret_cast<void (**)()>(
            static_cast<char*>(base_) + init_array_);
        size_t count = init_arraysz_ / sizeof(void*);
        for (size_t i = 0; i < count; ++i) {
            if (array[i]) {
                array[i]();
            }
        }
    }
}

void ElfLoader::executeFini() {
    if (!base_) return;

    // thực thi dt_fini_array nếu có (theo thứ tự ngược lại theo đặc tả elf)
    if (fini_array_ != 0 && fini_arraysz_ > 0) {
        auto** array = reinterpret_cast<void (**)()>(
            static_cast<char*>(base_) + fini_array_);
        size_t count = fini_arraysz_ / sizeof(void*);
        for (size_t i = count; i > 0; --i) {
            if (array[i - 1]) {
                array[i - 1]();
            }
        }
    }

    // thực thi dt_fini nếu có
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

    // eh_frame_vaddr_ trỏ tới .eh_frame_hdr (pt_gnu_eh_frame)
    auto* hdr = reinterpret_cast<const uint8_t*>(static_cast<char*>(base_) + eh_frame_vaddr_);
    
    // định dạng phần đầu:
    // uint8_t version; (phải là 1)
    // uint8_t eh_frame_ptr_enc;
    // uint8_t fde_count_enc;
    // uint8_t table_enc;
    
    if (hdr[0] != 1) return; // phiên bản không xác định
    
    // dw_eh_pe_pcrel | dw_eh_pe_sdata4 (0x1b) là phổ biến nhất
    if (hdr[1] == 0x1B) {
        // con trỏ là độ dời có dấu 32-bit từ địa chỉ của con trỏ
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
    if (hdr[0] != 1) return;
    
    if (hdr[1] == 0x1B) {
        const int32_t* ptr_addr = reinterpret_cast<const int32_t*>(hdr + 4);
        int32_t offset = *ptr_addr;
        void* eh_frame_actual = reinterpret_cast<void*>(
            reinterpret_cast<uintptr_t>(ptr_addr) + offset);
        
        __deregister_frame(eh_frame_actual);
    }
}

} // namespace kudroid