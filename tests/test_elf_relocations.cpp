#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"

#include <elf.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <vector>
#include <unistd.h>

static int failures;
static void check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    failures += !ok;
}

static void sleb(std::vector<uint8_t>& bytes, int64_t value) {
    for (;;) {
        uint8_t byte = value & 127;
        value >>= 7;
        const bool done = (value == 0 && !(byte & 64)) || (value == -1 && (byte & 64));
        bytes.push_back(byte | (done ? 0 : 128));
        if (done) break;
    }
}

static std::vector<char> fixture(bool packed, uint64_t origin = 0, bool legacyRelr = false) {
    std::vector<char> bytes(8192);
    auto* header = reinterpret_cast<Elf64_Ehdr*>(bytes.data());
    std::memcpy(header->e_ident, "\177ELF\2\1\1", 7);
    header->e_type = ET_DYN;
    header->e_machine = EM_AARCH64;
    header->e_version = EV_CURRENT;
    header->e_ehsize = sizeof(*header);
    header->e_phoff = sizeof(*header);
    header->e_phentsize = sizeof(Elf64_Phdr);
    header->e_phnum = 2;
    auto* phdr = reinterpret_cast<Elf64_Phdr*>(bytes.data() + header->e_phoff);
    phdr[0] = {PT_LOAD, PF_R | PF_W, 0, origin, 0, bytes.size(), bytes.size(), 4096};
    phdr[1] = {PT_DYNAMIC, PF_R | PF_W, 0x180, origin + 0x180, 0, 0x200, 0x200, 8};
    auto* dyn = reinterpret_cast<Elf64_Dyn*>(bytes.data() + 0x180);
    size_t index = 0;
    auto tag = [&](int64_t key, uint64_t value) { dyn[index].d_tag = key; dyn[index++].d_un.d_val = value; };
    tag(DT_SYMTAB, origin + 0x400);
    tag(DT_STRTAB, origin + 0x480);
    const char names[] = "\0optional_undefined_tls\0memcpy\0";
    std::memcpy(bytes.data() + 0x480, names, sizeof(names));
    tag(DT_STRSZ, sizeof(names));
    tag(DT_HASH, origin + 0x580);
    tag(DT_INIT_ARRAY, origin + 0xa00);
    tag(DT_INIT_ARRAYSZ, 8);
    auto* symbols = reinterpret_cast<Elf64_Sym*>(bytes.data() + 0x400);
    symbols[1].st_name = 1;
    symbols[1].st_info = ELF64_ST_INFO(STB_WEAK, STT_FUNC);
    symbols[2].st_name = 24;
    symbols[2].st_info = ELF64_ST_INFO(STB_WEAK, STT_FUNC);
    symbols[3].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT);
    symbols[3].st_shndx = SHN_ABS;
    symbols[3].st_value = 0x1234;
    auto* hash = reinterpret_cast<uint32_t*>(bytes.data() + 0x580);
    hash[0] = 1;
    hash[1] = 4;
    if (legacyRelr) {
        tag(0x6fffe000, origin + 0x600);
        tag(0x6fffe001, 16);
        tag(0x6fffe003, 8);
        auto* relr = reinterpret_cast<uint64_t*>(bytes.data() + 0x600);
        relr[0] = origin + 0xa00;
        relr[1] = 3;
        *reinterpret_cast<uint64_t*>(bytes.data() + 0xa00) = origin + 0x900;
        *reinterpret_cast<uint64_t*>(bytes.data() + 0xa08) = origin + 0x908;
    } else if (packed) {
        std::vector<uint8_t> stream{'A', 'P', 'S', '2'};
        for (int64_t value : {int64_t(6), int64_t(0), int64_t(6), int64_t(8),
                             int64_t(origin + 0xa00), int64_t(R_AARCH64_RELATIVE), int64_t(origin + 0x900),
                             int64_t(8), int64_t(ELF64_R_INFO(1, R_AARCH64_GLOB_DAT)), -int64_t(origin + 0x900),
                             int64_t(8), int64_t(ELF64_R_INFO(2, R_AARCH64_GLOB_DAT)), int64_t(0),
                             int64_t(8), int64_t(ELF64_R_INFO(1, R_AARCH64_ABS64)), int64_t(37),
                             int64_t(8), int64_t(R_AARCH64_ABS64), int64_t(16),
                             int64_t(8), int64_t(ELF64_R_INFO(3, R_AARCH64_ABS64)), int64_t(-53)}) sleb(stream, value);
        std::memcpy(bytes.data() + 0x600, stream.data(), stream.size());
        tag(0x60000011, origin + 0x600);
        tag(0x60000012, stream.size());
    } else {
        auto* rela = reinterpret_cast<Elf64_Rela*>(bytes.data() + 0x600);
        rela[0] = {origin + 0xa00, R_AARCH64_RELATIVE, int64_t(origin + 0x900)};
        rela[1] = {origin + 0xa08, ELF64_R_INFO(1, R_AARCH64_GLOB_DAT), 0};
        rela[2] = {origin + 0xa10, ELF64_R_INFO(2, R_AARCH64_GLOB_DAT), 0};
        rela[3] = {origin + 0xa18, ELF64_R_INFO(1, R_AARCH64_ABS64), 37};
        rela[4] = {origin + 0xa20, R_AARCH64_ABS64, 53};
        rela[5] = {origin + 0xa28, ELF64_R_INFO(3, R_AARCH64_ABS64), 0};
        tag(DT_RELA, origin + 0x600);
        tag(DT_RELASZ, 6 * sizeof(Elf64_Rela));
        tag(DT_RELAENT, sizeof(Elf64_Rela));
    }
    tag(DT_NULL, 0);
    return bytes;
}

static std::vector<char> binding_fixture() {
    auto bytes = fixture(false);
    auto* symbols = reinterpret_cast<Elf64_Sym*>(bytes.data() + 0x400);
    symbols[2].st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC);
    symbols[2].st_shndx = 1;
    symbols[2].st_value = 0x990;
    auto* dynamic = reinterpret_cast<Elf64_Dyn*>(bytes.data() + 0x180);
    size_t index = 0;
    while (dynamic[index].d_tag) ++index;
    dynamic[index++] = {DT_JMPREL, {0x780}};
    dynamic[index++] = {DT_PLTRELSZ, {sizeof(Elf64_Rela) * 2}};
    dynamic[index] = {DT_NULL, {0}};
    auto* entries = reinterpret_cast<Elf64_Rela*>(bytes.data() + 0x780);
    entries[0] = {0xb00, ELF64_R_INFO(2, R_AARCH64_JUMP_SLOT), 0};
    entries[1] = {0xb08, ELF64_R_INFO(2, R_AARCH64_JUMP_SLOT), 32};
    return bytes;
}

static void interposition_fixture(const std::function<void(std::vector<char>&)>& mutate, bool allowed) {
    auto bytes = binding_fixture();
    mutate(bytes);
    char path[] = "/tmp/kudroid-elf-bindings-XXXXXX";
    int fd = mkstemp(path);
    close(fd);
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
    kudroid::ElfLoader loader(path);
    check(loader.parse() && loader.map(), "function-binding fixture maps");
    check(!loader.interposeFunction("memcpy", reinterpret_cast<void*>(0x76540000)),
          "interposition before relocation is rejected");
    bool entered = false;
    const bool relocated = loader.relocate([&] {
        entered = true;
        auto* base = static_cast<char*>(loader.baseAddress());
        auto* slots = reinterpret_cast<uint64_t*>(base + 0xb00);
        const uint64_t first = slots[0], second = slots[1];
        const uint64_t ordinary = *reinterpret_cast<uint64_t*>(base + 0xa10);
        check(*reinterpret_cast<void**>(base + 0xa00) == base + 0x900,
              "ordinary relocations finish before finalization");
        check(loader.interposeFunction("memcpy", reinterpret_cast<void*>(0x76540000)) == allowed,
              "only preemptable function bindings accept interposition");
        check(allowed ? slots[0] == 0x76540000 && slots[1] == 0x76540020 : slots[0] == first && slots[1] == second,
              "matching slots update with addends or remain transactionally unchanged");
        check(*reinterpret_cast<uint64_t*>(base + 0xa10) == ordinary,
              "non-PLT relocations retain original binding");
        check(!loader.interposeFunction("absent", reinterpret_cast<void*>(1)), "absent function binding is rejected");
        return true;
    });
    check(relocated && entered, "binding finalizer runs before final protections");
    check(!loader.interposeFunction("memcpy", reinterpret_cast<void*>(1)), "interposition after finalization is rejected");
    unlink(path);
}

static void import_scope_fixture() {
    auto source = binding_fixture();
    char sourcePath[] = "/tmp/kudroid-elf-scope-source-XXXXXX";
    int fd = mkstemp(sourcePath);
    close(fd);
    std::ofstream(sourcePath, std::ios::binary).write(source.data(), source.size());
    kudroid::ElfLoader supplier(sourcePath);
    check(supplier.parse() && supplier.map() && supplier.relocate(), "supplying import scope loads");
    auto consumer = binding_fixture();
    auto* symbol = reinterpret_cast<Elf64_Sym*>(consumer.data() + 0x400) + 2;
    symbol->st_shndx = SHN_UNDEF;
    symbol->st_value = 0;
    char consumerPath[] = "/tmp/kudroid-elf-scope-consumer-XXXXXX";
    fd = mkstemp(consumerPath);
    close(fd);
    std::ofstream(consumerPath, std::ios::binary).write(consumer.data(), consumer.size());
    kudroid::ElfLoader dependent(consumerPath);
    dependent.setImportScope(&supplier);
    const bool loaded = dependent.parse() && dependent.map() && dependent.relocate();
    check(loaded, "dependent image relocates with a supplying import scope");
    if (loaded) {
        auto** binding = reinterpret_cast<void**>(static_cast<char*>(dependent.baseAddress()) + 0xb00);
        check(*binding == supplier.getSymbolAddress("memcpy"), "imports prefer the supplying runtime definition");
    }
    unlink(sourcePath);
    unlink(consumerPath);
}

static void verify_image(bool packed, uint64_t origin, bool legacyRelr = false) {
    char path[] = "/tmp/kudroid-elf-relocations-XXXXXX";
    const int fd = mkstemp(path);
    close(fd);
    const auto bytes = fixture(packed, origin, legacyRelr);
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
    kudroid::ElfLoader loader(path);
    const bool ok = loader.parse() && loader.map() && loader.relocate();
    check(ok, "ELF fixture maps and relocates");
    if (!ok) std::printf("loader error: %s\n", loader.lastError());
    if (ok) {
        auto* base = static_cast<char*>(loader.baseAddress());
        auto* slots = reinterpret_cast<uint64_t*>(base + origin + 0xa00);
        check(slots[0] == reinterpret_cast<uintptr_t>(base + origin + 0x900),
              "constructor pointer includes load bias");
        if (legacyRelr) {
            check(slots[1] == reinterpret_cast<uintptr_t>(base + origin + 0x908),
                  "legacy Android RELR bitmap includes load bias");
        } else {
            check(slots[1] == 0, "unresolved weak TLS hook remains null");
            check(slots[2] == reinterpret_cast<uintptr_t>(kudroid::resolve_bionic_symbol("memcpy")),
                  "available weak imports bind to real implementations");
            check(slots[3] == 37, "undefined weak symbol retains the relocation addend");
            check(slots[4] == 53, "symbol index zero retains the relocation addend");
            check(slots[5] == 0x1234, "absolute symbol is not adjusted by the load bias");
        }
    }
    unlink(path);
}

static void rejects(const std::function<void(std::vector<char>&)>& corrupt, const char* message) {
    auto bytes = fixture(false);
    corrupt(bytes);
    char path[] = "/tmp/kudroid-elf-invalid-XXXXXX";
    close(mkstemp(path));
    std::ofstream(path, std::ios::binary).write(bytes.data(), bytes.size());
    kudroid::ElfLoader loader(path);
    check(loader.parse() && loader.map() && !loader.relocate(), message);
    unlink(path);
}

#ifdef KUDROID_PACKED_ELF_FIXTURE
static void linker_fixture() {
    std::ifstream input(KUDROID_PACKED_ELF_FIXTURE, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input), {}};
    check(bytes.size() >= sizeof(Elf64_Ehdr), "linker-generated ARM64 fixture exists");
    if (bytes.size() < sizeof(Elf64_Ehdr)) return;
    const auto* header = reinterpret_cast<const Elf64_Ehdr*>(bytes.data());
    const auto* phdrs = reinterpret_cast<const Elf64_Phdr*>(bytes.data() + header->e_phoff);
    uint64_t array = 0, arraySize = 0, packedSize = 0;
    for (size_t index = 0; index < header->e_phnum; ++index) {
        if (phdrs[index].p_type != PT_DYNAMIC) continue;
        const auto* dynamic = reinterpret_cast<const Elf64_Dyn*>(bytes.data() + phdrs[index].p_offset);
        for (size_t item = 0; item < phdrs[index].p_filesz / sizeof(Elf64_Dyn); ++item) {
            if (dynamic[item].d_tag == DT_INIT_ARRAY) array = dynamic[item].d_un.d_val;
            if (dynamic[item].d_tag == DT_INIT_ARRAYSZ) arraySize = dynamic[item].d_un.d_val;
            if (dynamic[item].d_tag == 0x60000012) packedSize = dynamic[item].d_un.d_val;
        }
    }
    check(packedSize && arraySize == 8, "real linker emits APS2 constructor relocations");
    kudroid::ElfLoader loader(KUDROID_PACKED_ELF_FIXTURE);
    const bool loaded = loader.parse() && loader.map() && loader.relocate();
    check(loaded, "real linker-generated ARM64 image relocates");
    if (!loaded) { std::printf("loader error: %s\n", loader.lastError()); return; }
    auto* base = static_cast<char*>(loader.baseAddress());
    check(*reinterpret_cast<void**>(base + array) == loader.getSymbolAddress("initialize"),
          "real constructor pointer resolves to executable symbol");
    auto** pointers = static_cast<void**>(loader.getSymbolAddress("pointers"));
    bool all = pointers;
    for (int index = 0; index < 4 && all; ++index) all = pointers[index] == loader.getSymbolAddress("initialized");
    check(all, "real packed data pointers resolve to loaded globals");
}
#endif

int main(int argc, char** argv) {
#ifdef KUDROID_PACKED_ELF_FIXTURE
    if (argc == 2 && std::strcmp(argv[1], "--packed-only") == 0) {
        linker_fixture();
        return failures ? 1 : 0;
    }
#endif
    interposition_fixture([](auto&) {}, true);
    interposition_fixture([](auto& bytes) {
        (reinterpret_cast<Elf64_Sym*>(bytes.data() + 0x400) + 2)->st_other = STV_PROTECTED;
    }, false);
    interposition_fixture([](auto& bytes) {
        (reinterpret_cast<Elf64_Sym*>(bytes.data() + 0x400) + 2)->st_info = ELF64_ST_INFO(STB_LOCAL, STT_FUNC);
    }, false);
    interposition_fixture([](auto& bytes) {
        auto* dyn = reinterpret_cast<Elf64_Dyn*>(bytes.data() + 0x180);
        while (dyn->d_tag) ++dyn;
        *dyn++ = {DT_SYMBOLIC, {0}};
        *dyn = {DT_NULL, {0}};
    }, false);
    interposition_fixture([](auto& bytes) {
        auto* symbols = reinterpret_cast<Elf64_Sym*>(bytes.data() + 0x400);
        symbols[3] = symbols[2];
        symbols[3].st_other = STV_PROTECTED;
        (reinterpret_cast<Elf64_Rela*>(bytes.data() + 0x780) + 1)->r_info = ELF64_R_INFO(3, R_AARCH64_JUMP_SLOT);
    }, false);
    import_scope_fixture();
    verify_image(true, 0);
    verify_image(false, 0);
    verify_image(true, 0, true);
    verify_image(true, 0x10000);
    verify_image(false, 0x10000);
    verify_image(true, 0x10000, true);
    rejects([](auto& bytes) { reinterpret_cast<Elf64_Rela*>(bytes.data() + 0x600)->r_offset = UINT64_MAX; },
            "relocation outside mapped segments is rejected");
    rejects([](auto& bytes) {
        auto* dyn = reinterpret_cast<Elf64_Dyn*>(bytes.data() + 0x180);
        for (; dyn->d_tag; ++dyn) if (dyn->d_tag == DT_RELAENT) dyn->d_un.d_val = 0;
    }, "zero relocation entry size is rejected before division");
    rejects([](auto& bytes) {
        auto* dyn = reinterpret_cast<Elf64_Dyn*>(bytes.data() + 0x180);
        for (; dyn->d_tag; ++dyn) if (dyn->d_tag == DT_INIT_ARRAYSZ) dyn->d_un.d_val = UINT64_MAX;
    }, "initializer array outside mapped segments is rejected");
    kudroid::LibraryManager manager;
    check(!manager.resolveGlobalSymbol("optional_lookup_absent", false), "optional global lookup remains null");
    check(kudroid::is_universal_dummy(manager.resolveGlobalSymbol("optional_lookup_absent")),
          "ordinary lookup still supplies fallback after optional miss");
    check(!manager.resolveGlobalSymbol("optional_lookup_absent", false),
          "cached dummy never becomes a weak definition");
#ifdef KUDROID_PACKED_ELF_FIXTURE
    linker_fixture();
#endif
    return failures ? 1 : 0;
}
