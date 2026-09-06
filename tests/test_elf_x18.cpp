// test_elf_x18.cpp — the AOT x18 rewrite must preserve semantics exactly.
//
// Why this exists. Darwin reserves x18 as a platform register and does not
// preserve it across async delivery, while Android NDK code uses it as a
// normal scratch register. A mixer loop computing a pointer in x18 faulted
// with x18 == 0 while its sibling registers stayed valid — unfixable at the
// fault site (the value is gone), so guest code is rewritten at load to never
// observe x18 live. A wrong rewrite corrupts every guest, so this drives the
// real entry point against synthetic ELFs with hand-built encodings.
#include "kudroid/ElfX18.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/elf_loader.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void putUleb(std::vector<std::uint8_t>& b, std::uint64_t v) {
    do {
        unsigned c = v & 0x7F;
        v >>= 7;
        b.push_back(static_cast<std::uint8_t>(c | (v ? 0x80 : 0)));
    } while (v);
}

// A synthetic ELF64: one RX LOAD (vaddr == offset), .shstrtab + .eh_frame,
// code words supplied by the caller. Returned with mapping laid out so that
// mapped[vaddr] works with minVaddr 0.
struct SynthElf {
    std::vector<std::uint8_t> file;
    std::vector<std::uint8_t> mapped;
    std::vector<kudroid::ElfLoader::Segment> segs;
    std::size_t codeOff = 0;

    SynthElf(const std::vector<std::uint32_t>& code, bool personality) {
        codeOff = 0x1000;
        const std::size_t codeBytes = code.size() * 4;
        // eh_frame goes after the code in the same LOAD.
        const std::size_t ehOff = codeOff + codeBytes;
        EhBuilder eh;
        eh.cie(personality);
        eh.fde(codeOff, codeBytes, ehOff);
        // Section names.
        const std::string shstr("\0.shstrtab\0.eh_frame\0", 22);
        const std::size_t shstrOff = ehOff + eh.bytes.size();
        const std::size_t shOff = (shstrOff + shstr.size() + 7) & ~7ULL;
        file.assign(shOff + 3 * 64, 0);
        // ELF header.
        file[0] = 0x7F;
        file[1] = 'E';
        file[2] = 'L';
        file[3] = 'F';
        file[4] = 2;
        file[5] = 1;
        file[6] = 1;
        auto w16 = [&](std::size_t o, std::uint16_t v) { std::memcpy(file.data() + o, &v, 2); };
        auto w64 = [&](std::size_t o, std::uint64_t v) { std::memcpy(file.data() + o, &v, 8); };
        w16(0x3A, 64);
        w16(0x3C, 3);
        w16(0x3E, 1);
        w64(0x28, shOff);
        // Program header omitted: segments_ is supplied directly below.
        std::memcpy(file.data() + codeOff, code.data(), codeBytes);
        std::memcpy(file.data() + ehOff, eh.bytes.data(), eh.bytes.size());
        std::memcpy(file.data() + shstrOff, shstr.data(), shstr.size());
        // Section 1: .shstrtab.
        w64(shOff + 64 + 24, shstrOff);
        w64(shOff + 64 + 32, shstr.size());
        // Section 2: .eh_frame (name offset 11).
        auto w32 = [&](std::size_t o, std::uint32_t v) { std::memcpy(file.data() + o, &v, 4); };
        w32(shOff + 128 + 0, 11);
        w64(shOff + 128 + 16, ehOff);  // sh_addr == file offset (minVaddr 0)
        w64(shOff + 128 + 24, ehOff);
        w64(shOff + 128 + 32, eh.bytes.size());
        mapped = file;  // identity map for the test
        kudroid::ElfLoader::Segment seg;
        seg.vaddr = 0;
        seg.offset = 0;
        seg.filesz = file.size();
        seg.memsz = file.size();
        seg.flags = 5;  // R+X
        segs.push_back(seg);
    }

    kudroid::X18Stats run() {
        return kudroid::elf_x18_rewrite(mapped.data(), 0, segs, file.data(), file.size());
    }
    std::uint32_t word(std::size_t i) const {
        std::uint32_t w = 0;
        std::memcpy(&w, mapped.data() + codeOff + i * 4, 4);
        return w;
    }

    // Minimal .eh_frame builder: one CIE + one FDE.
    struct EhBuilder {
        std::vector<std::uint8_t> bytes;
        std::size_t cieOff = 0;
        void cie(bool withPersonality) {
            cieOff = bytes.size();
            const std::size_t lenPos = bytes.size();
            put32(bytes, 0);
            put32(bytes, 0);  // CIE id
            bytes.push_back(1);
            if (withPersonality) {
                bytes.push_back('z');
                bytes.push_back('P');
                bytes.push_back('L');
                bytes.push_back('R');
                bytes.push_back(0);
            } else {
                bytes.push_back('z');
                bytes.push_back('R');
                bytes.push_back(0);
            }
            putUleb(bytes, 1);
            putUleb(bytes, 0x7C);  // -4
            putUleb(bytes, 30);
            if (withPersonality) {
                putUleb(bytes, 2);
                bytes.push_back(0x1B);
                bytes.push_back(0);
                bytes.push_back(0x1B);
            } else {
                putUleb(bytes, 1);
                bytes.push_back(0x1B);
            }
            bytes.push_back(0x00);
            while ((bytes.size() - lenPos - 4) % 4 != 0) bytes.push_back(0x00);
            std::uint32_t len = static_cast<std::uint32_t>(bytes.size() - lenPos - 4);
            std::memcpy(bytes.data() + lenPos, &len, 4);
        }
        void fde(std::uint64_t start, std::uint64_t size, std::uint64_t ehBase) {
            const std::size_t lenPos = bytes.size();
            put32(bytes, 0);
            const std::size_t fieldPos = bytes.size();
            put32(bytes, static_cast<std::uint32_t>(fieldPos - cieOff));
            const std::size_t locPos = bytes.size();
            // pcrel: relative to the field's final address (ehBase + locPos).
            std::int32_t rel = static_cast<std::int32_t>(start - (ehBase + locPos));
            put32(bytes, static_cast<std::uint32_t>(rel));
            put32(bytes, static_cast<std::uint32_t>(size));
            while ((bytes.size() - lenPos - 4) % 4 != 0) bytes.push_back(0x00);
            std::uint32_t len = static_cast<std::uint32_t>(bytes.size() - lenPos - 4);
            std::memcpy(bytes.data() + lenPos, &len, 4);
        }
    };
};

// add x18, x0, x1 with x0 live: substitute must avoid x0 (expect x15).
void test_add_renamed() {
    std::printf("[rewrite] add x18 is renamed, siblings untouched\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;
    SynthElf elf({add, 0xD65F03C0}, false);  // add x18,x0,x1; ret
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 1, "one function, one site");
    const std::uint32_t out = elf.word(0);
    Check(((out >> 0) & 31) == 15, "Rd became x15");
    Check(((out >> 5) & 31) == 0 && ((out >> 16) & 31) == 1, "Rn/Rm kept");
    Check(elf.word(1) == 0xD65F03C0, "ret untouched");
}

// No free register: x0-x15 all touched, x18 used -> skip, bytes identical.
void test_no_free_reg_skips() {
    std::printf("[rewrite] full pressure skips without touching bytes\n");
    std::vector<std::uint32_t> code;
    for (unsigned r = 0; r <= 15; ++r) {
        code.push_back(0x91000000 | (r << 0) | (r << 5));  // add x_r, x_r, #0
    }
    code.push_back(0x8B010000 | (1u << 16) | (0u << 5) | 18u);  // add x18,x0,x1
    code.push_back(0xD65F03C0);
    SynthElf elf(code, false);
    std::vector<std::uint32_t> before(code.size());
    for (std::size_t i = 0; i < code.size(); ++i) before[i] = elf.word(i);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0 && st.skippedNoReg == 1, "skipped for no free reg");
    bool same = true;
    for (std::size_t i = 0; i < code.size(); ++i) same &= elf.word(i) == before[i];
    Check(same, "bytes untouched on skip");
}

// br x18 is renamed when its target is defined in-function.
void test_branch_renamed() {
    std::printf("[rewrite] br x18 is renamed\n");
    const std::uint32_t adrp = 0x90000012;  // adrp x18, #0
    const std::uint32_t br = 0xD61F0000 | (18u << 5);  // br x18
    SynthElf elf({adrp, br}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "rewritten, def + use");
    Check(((elf.word(0) >> 0) & 31) == 15, "adrp Rd became x15");
    Check(((elf.word(1) >> 5) & 31) == 15, "br target became x15");
}

// br x18 alone reads a value from outside: must skip, not rename.
void test_branch_bare_skips() {
    std::printf("[rewrite] lone br x18 is skipped\n");
    const std::uint32_t br = 0xD61F0000 | (18u << 5);  // br x18
    SynthElf elf({br}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0, "live-in skipped");
    Check(elf.word(0) == br, "bytes untouched");
}

// Trailing define before a closed end (ret): dead, safe to rename.
void test_liveout_skips() {
    std::printf("[rewrite] trailing x18 define before ret is renamed\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;
    SynthElf elf({add, 0xD65F03C0}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 1, "dead def rewritten");
    Check(((elf.word(0) >> 0) & 31) == 15, "Rd became x15");
}

// A call inside the define-use span: the substitute would not survive it.
void test_callspan_skips() {
    std::printf("[rewrite] call inside the span is skipped\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;
    const std::uint32_t use = 0x8B020240;  // add x0, x18, x2
    SynthElf elf({add, 0x94000000, use}, false);  // def; bl; use
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0, "call-span skipped");
    Check(elf.word(0) == add && elf.word(2) == use, "bytes untouched");
}

// x18 through memory: provenance unprovable, skip.
void test_mem_skips() {
    std::printf("[rewrite] x18 memory lane is skipped\n");
    const std::uint32_t str = 0xF9000000 | (18u << 0) | (0u << 5);  // str x18,[x0]
    SynthElf elf({str}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0, "mem skipped");
    Check(elf.word(0) == str, "bytes untouched");
}

// Personality CIE: skipped even with x18 present.
void test_personality_skips() {
    std::printf("[rewrite] personality FDE is skipped\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;
    SynthElf elf({add}, true);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0 && st.skippedEh == 1, "personality skipped");
    Check(elf.word(0) == add, "bytes untouched");
}

// Crash-loop shape: the mixer prologue pattern renames consistently.
void test_crash_loop_shape() {
    std::printf("[rewrite] multi-use x18 renames to one substitute\n");
    std::vector<std::uint32_t> code = {
        0x8B090C0F,  // add x15, x0, x9 (control: no x18)
        0xF94029ED,  // ldr x13, [x15, #0x50]
        0x8B0C01F2,  // add x18, x25, x12 (synthetic: Rd=x18)
        0xAD3F8240,  // stp q0, q0, [x18, #-0x10] (SIMD pair: integer base)
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    // Used: x0,x9,x12,x13,x15,x25 -> substitute x14, both sites consistently.
    Check(st.rewritten == 1 && st.sites == 2, "rewritten, two sites");
    Check(((elf.word(2) >> 0) & 31) == 14, "add Rd became x14");
    Check(((elf.word(3) >> 5) & 31) == 14, "stp base became x14");
    Check(elf.word(0) == 0x8B090C0F && elf.word(1) == 0xF94029ED,
          "control words untouched");
}

}  // namespace

int main(int argc, char** argv) {
    // Ops mode: rewrite stats for a real .so (map() runs the loader hook).
    if (argc > 1) {
        kudroid::ElfLoader loader(argv[1]);
        if (!loader.parse() || !loader.map()) {
            std::printf("load failed: %s\n", loader.lastError());
            return 1;
        }
        std::printf("mapped ok\n");
        return 0;
    }
    std::printf("=== KuDroid x18 rewrite ===\n");
    test_add_renamed();
    test_no_free_reg_skips();
    test_branch_renamed();
    test_branch_bare_skips();
    test_liveout_skips();
    test_callspan_skips();
    test_mem_skips();
    test_personality_skips();
    test_crash_loop_shape();
    if (g_failures == 0) {
        std::printf("=== PASSED (%d checks) ===\n", g_checks);
        return 0;
    }
    std::printf("=== FAILED (%d of %d) ===\n", g_failures, g_checks);
    return 1;
}
