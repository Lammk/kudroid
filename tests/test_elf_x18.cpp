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

    SynthElf(const std::vector<std::uint32_t>& code, bool personality,
             std::size_t fdeBytes = static_cast<std::size_t>(-1)) {
        codeOff = 0x1000;
        const std::size_t codeBytes = code.size() * 4;
        // eh_frame goes after the code in the same LOAD.
        const std::size_t ehOff = codeOff + codeBytes;
        EhBuilder eh;
        eh.cie(personality);
        // fdeBytes < codeBytes leaves the tail FDE-uncovered (gap path).
        const std::size_t cov =
            (fdeBytes == static_cast<std::size_t>(-1)) ? codeBytes : fdeBytes;
        if (cov != codeBytes) eh.fde(0, codeOff, ehOff);  // gap starts at code
        eh.fde(codeOff, cov, ehOff);
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

// No free register within the span window: x0-x15 all touched between the
// define and the use, so per-span allocation has nowhere to borrow -> skip,
// bytes identical.
void test_no_free_reg_skips() {
    std::printf("[rewrite] full pressure inside the span skips without touching bytes\n");
    std::vector<std::uint32_t> code;
    code.push_back(0x8B010000 | (1u << 16) | (0u << 5) | 18u);  // add x18,x0,x1 (def, span start)
    for (unsigned r = 0; r <= 15; ++r) {
        // add x_r, x_r, #0 — every caller-saved register busy inside the span.
        code.push_back(0x91000000 | (r << 0) | (r << 5));
    }
    code.push_back(0x8B020240);  // add x0, x18, x2 (use, span end)
    code.push_back(0xD65F03C0);  // ret
    SynthElf elf(code, false);
    std::vector<std::uint32_t> before(code.size());
    for (std::size_t i = 0; i < code.size(); ++i) before[i] = elf.word(i);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0 && st.skippedNoReg == 1, "skipped for no free reg");
    bool same = true;
    for (std::size_t i = 0; i < code.size(); ++i) same &= elf.word(i) == before[i];
    Check(same, "bytes untouched on skip");
}

// Pressure OUTSIDE the span no longer blocks: a register busy elsewhere in
// the function but free inside the window is borrowable (span-level
// allocation), and a register defined before and read after the window is
// still refused (crossing live range).
void test_pressure_outside_span_borrows() {
    std::printf("[rewrite] outside-span pressure borrows; crossing range does not\n");
    // Span borrows x15 despite x15 being busy earlier: its value is dead by
    // the span (redefined before the span, never read after).
    std::vector<std::uint32_t> code;
    code.push_back(0x910001EF);  // 0: add x15, x15, #0 (busy early, dead after)
    code.push_back(0x8B010000 | (1u << 16) | (0u << 5) | 18u);  // 1: def x18
    code.push_back(0x8B020240);  // 2: use x18
    code.push_back(0xD65F03C0);  // 3: ret
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "borrowed a register busy outside the span");
    Check(((elf.word(1) >> 0) & 31) == 15 && ((elf.word(2) >> 5) & 31) == 15,
          "span picked x15");

    // Crossing live range: x14 defined before the span and read after it —
    // must NOT be borrowed; with every other caller-saved register also
    // crossing/touched, the function is skipped.
    std::vector<std::uint32_t> code2;
    code2.push_back(0x910001EE);  // 0: add x14, x14, #0 (def, crossing)
    // Keep x0..x13 and x15 busy inside the span window.
    for (unsigned r = 0; r <= 13; ++r) {
        code2.push_back(0x91000000 | (r << 0) | (r << 5));  // add x_r,x_r,#0
    }
    code2.push_back(0x910001EF);                 // add x15, x15, #0 (in-window)
    const std::size_t defAt = code2.size();
    code2.push_back(0x8B010000 | (1u << 16) | (0u << 5) | 18u);  // def x18
    for (unsigned r = 0; r <= 13; ++r) {
        code2.push_back(0x91000000 | (r << 0) | (r << 5));  // still busy after
    }
    code2.push_back(0x910001EE);  // use x14 AFTER the span (crossing read)
    const std::size_t useAt = code2.size();
    code2.push_back(0x8B020240);  // use x18 (closes the span)
    code2.push_back(0xD65F03C0);  // ret
    SynthElf elf2(code2, false);
    std::vector<std::uint32_t> before(code2.size());
    for (std::size_t i = 0; i < code2.size(); ++i) before[i] = elf2.word(i);
    kudroid::X18Stats st2 = elf2.run();
    Check(st2.rewritten == 0 && st2.skippedNoReg >= 1,
          "crossing x14 refused; no substitute left");
    bool same = true;
    for (std::size_t i = 0; i < code2.size(); ++i) same &= elf2.word(i) == before[i];
    Check(same, "bytes untouched on crossing refusal");
    (void)defAt;
    (void)useAt;
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

// Gap path (no FDE): loop-head define above the loop renames with the loop.
void test_gap_loop_renamed() {
    std::printf("[rewrite] gap loop with outside define is renamed\n");
    std::vector<std::uint32_t> code = {
        0x910041F2,  // 0: add x18, x15, #0x10 (feed define)
        0xAA0B03E1,  // 1: mov x1, x11
        0xF9400240,  // 2: ldr x0, [x18]
        0x91008252,  // 3: add x18, x18, #0x20 (loop step)
        0xF1002021,  // 4: subs x1, x1, #8
        0x54FFFFA1,  // 5: b.ne -> 2
        0xD65F03C0,  // 6: ret (value dies)
        0x17FFFFF9,  // 7: b -> 0 (outside entry, keeps chunk split-free)
    };
    SynthElf elf(code, false, 0);  // FDE covers nothing: all gap
    kudroid::X18Stats st = elf.run();
    // Used: x0,x1,x11,x15 -> substitute x14, four sites consistently.
    Check(st.rewritten == 1 && st.sites == 4, "gap loop rewritten, four sites");
    Check(((elf.word(0) >> 0) & 31) == 14, "define Rd became x14");
    Check(((elf.word(2) >> 5) & 31) == 14, "ldr base became x14");
    Check(((elf.word(3) >> 0) & 31) == 14 && ((elf.word(3) >> 5) & 31) == 14,
          "step Rd/Rn became x14");
    Check(elf.word(1) == 0xAA0B03E1 && elf.word(4) == 0xF1002021 &&
              elf.word(5) == 0x54FFFFA1 && elf.word(6) == 0xD65F03C0 &&
              elf.word(7) == 0x17FFFFF9,
          "control words untouched");
}

// Gap path: trailing define escaping to an unrenamed use is skipped.
void test_gap_escape_skipped() {
    std::printf("[rewrite] gap escape to unrenamed use is skipped\n");
    std::vector<std::uint32_t> code = {
        0x910041F2,  // 0: add x18, x15, #0x10
        0x34000041,  // 1: cbz x1, -> 3
        0xD65F03C0,  // 2: ret (dies here, escapes there)
        0xF9400240,  // 3: ldr x0, [x18] (outside the define's range)
        0xD65F03C0,  // 4: ret
    };
    SynthElf elf(code, false, 0);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0 && st.sites == 0, "escape skipped");
    for (std::size_t i = 0; i < code.size(); ++i)
        Check(elf.word(i) == code[i], "bytes untouched");
}

// ── plan ground truth: the Melon_0 convex-mesh crash ────────────────────────
// libunity 0xe5f788 shape: and x18, x9, #0xff; strb w14, [x22, x18] — a
// register-offset store whose index register is x18. Used to skip the whole
// function via slot-scan paths; must now rename.

void test_crash_shape_renamed() {
    std::printf("[rewrite] crash-log shape (and x18 / strb [x22,x18]) renames\n");
    std::vector<std::uint32_t> code = {
        0x92001D32,       // 0: and x18, x9, #0xff (def)
        0x38326ACE,       // 1: strb w14, [x22, x18] (use, the faulting site)
        0xD65F03C0,       // 2: ret
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "crash shape rewritten, two sites");
    Check(((elf.word(0) >> 0) & 31) == 15, "and Rd became x15");
    Check(((elf.word(1) >> 16) & 31) == 15, "strb Rm became x15");
    Check(elf.word(1) == (0x38326ACE & ~(31u << 16)) | (15u << 16),
          "only the Rm field moved");
}

// A branch can close a span (last use at a block boundary): cbz x18 and
// blr x18 are terminators, not escapes.
void test_branch_terminator_spans() {
    std::printf("[rewrite] cbz/blr close the span as terminators\n");
    std::vector<std::uint32_t> code = {
        0xAA000012,       // 0: mov x18, x0 (def)
        0x34000052,       // 1: cbz x18, +1 (use at the boundary; target 2)
        0xD65F03C0,       // 2: ret
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "cbz span rewritten");
    Check(((elf.word(0) >> 0) & 31) == 15, "mov Rd became x15");
    Check(((elf.word(1) >> 0) & 31) == 15, "cbz Rt became x15");

    std::vector<std::uint32_t> code2 = {
        0x910022B2,       // 0: add x18, x21, #8 (def; a load INTO x18 would be
                          // a memory lane and skip instead)
        0xD63F0240,       // 1: blr x18 (use: indirect call terminator)
        0xD65F03C0,       // 2: ret
    };
    SynthElf elf2(code2, false);
    kudroid::X18Stats st2 = elf2.run();
    Check(st2.rewritten == 1 && st2.sites == 2, "blr span rewritten");
    Check(((elf2.word(0) >> 0) & 31) == 15, "add Rd became x15");
    Check(((elf2.word(1) >> 5) & 31) == 15, "blr Rn became x15");
}

// Two independent spans in one block borrow substitutes independently; an
// x15-busy window inside span 2 forces it to fall to x14 while span 1 keeps
// x15 — the per-span allocation the old whole-function pick could not do.
void test_two_spans_one_block() {
    std::printf("[rewrite] two independent spans get independent substitutes\n");
    std::vector<std::uint32_t> code = {
        0xAA000012,       // 0: mov x18, x0          (span 1 def)
        0xB8326841,       // 1: str w1, [x2, x18]    (span 1 use)
        0xB0000012,       // 2: adrp x18, #0         (span 2 def)
        0x910001EF,       // 3: add x15, x15, #0     (x15 busy in span 2 only)
        0xF9400A46,       // 4: ldr x6, [x18, #0x10] (span 2 use)
        0xD65F03C0,       // 5: ret
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 4, "both spans rewritten");
    const unsigned s1 = (elf.word(0) >> 0) & 31;
    const unsigned s2 = (elf.word(2) >> 0) & 31;
    Check(s1 == 15, "span 1 took x15");
    Check(((elf.word(1) >> 16) & 31) == s1, "span 1 use shares it");
    Check(s2 == 14, "span 2 fell to x14 — x15 is busy inside its window");
    Check(((elf.word(4) >> 5) & 31) == s2, "span 2 use shares it");
}

// SIMD lanes are vector registers: v18 needs nothing, but an x18 BASE of a
// SIMD access is still the integer register and must rename.
void test_simd_lane_vs_base() {
    std::printf("[rewrite] SIMD v18 lanes stay; x18 base renames\n");
    // ldr q18, [sp]: Rt is a vector register — must be left completely alone
    // and must NOT skip the function.
    std::vector<std::uint32_t> code = {
        0x3DC003D2,       // 0: ldr q18, [sp] (vector lane, no x18 involved)
        0xAA000012,       // 1: mov x18, x0 (def)
        0x3DC00252,       // 2: ldr q18, [x18] — Rn=x18 GPR base, Rt v18 lane
        0xD65F03C0,       // 3: ret
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "SIMD function renamed, not skipped");
    Check(((elf.word(0) >> 0) & 31) == 18, "vector lane v18 untouched in word 0");
    Check(((elf.word(2) >> 0) & 31) == 18, "vector lane v18 untouched in word 2");
    Check(((elf.word(2) >> 5) & 31) == 15, "SIMD base x18 became x15");
}

// FP<->GPR crossover: fmov x18, d0 defines the GPR; fmov d1, x18 reads it.
// Pure vector-vector fmov (v18) is left alone.
void test_fmov_crossover() {
    std::printf("[rewrite] fmov crossover patches only the GPR side\n");
    std::vector<std::uint32_t> code = {
        0x9E660012,       // 0: fmov x18, d0 (FP -> GPR: Rd defines)
        0xD65F03C0,       // 1: ret
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 1, "fmov x18,d0 renamed");
    Check(((elf.word(0) >> 0) & 31) == 15, "Rd (GPR side) became x15");

    std::vector<std::uint32_t> code2 = {
        0xAA000012,       // 0: mov x18, x0 (def — a lone GPR->FP fmov reads
                          // live-in x18 and would skip instead)
        0x9E670241,       // 1: fmov d1, x18 (GPR -> FP: Rn reads)
        0xD65F03C0,       // 2: ret
    };
    SynthElf elf2(code2, false);
    kudroid::X18Stats st2 = elf2.run();
    Check(st2.rewritten == 1 && st2.sites == 2, "fmov d1,x18 renamed after a def");
    Check(((elf2.word(0) >> 0) & 31) == 15, "mov Rd became x15");
    Check(((elf2.word(1) >> 5) & 31) == 15, "Rn (GPR side) became x15");

    std::vector<std::uint32_t> code3 = {
        0x1E604012,       // 0: fmov d18, d0 (vector-vector: nothing to do)
        0xD65F03C0,       // 1: ret
    };
    SynthElf elf3(code3, false);
    kudroid::X18Stats st3 = elf3.run();
    Check(st3.rewritten == 0 && st3.sites == 0,
          "fmov d18,d0 touches no integer register");
    Check(elf3.word(0) == 0x1E604012, "bytes untouched");

    std::vector<std::uint32_t> code4 = {
        0x9E380052,       // 0: fcvtzs x18, s2 (FP -> GPR: Rd defines)
        0xD65F03C0,       // 1: ret
    };
    SynthElf elf4(code4, false);
    kudroid::X18Stats st4 = elf4.run();
    Check(st4.rewritten == 1 && st4.sites == 1, "fcvtzs x18,s2 renamed");
    Check(((elf4.word(0) >> 0) & 31) == 15, "Rd (GPR side) became x15");
}

// Pre-index writeback defines the base register; the writeback def lands in
// the substitute and the follow-up use of the advanced address must read the
// SAME substitute (one span, one substitute).
void test_writeback_preindex() {
    std::printf("[rewrite] pre-index writeback keeps one substitute\n");
    std::vector<std::uint32_t> code = {
        0xAA000012,       // 0: mov x18, x0 (def)
        0xF8410E40,       // 1: ldr x0, [x18, #16]! (use + writeback def of base)
        0x38200241,       // 2: strb w1, [x18] (use of advanced address)
        0xD65F03C0,       // 3: ret
    };
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 3,
          "writeback chain rewritten, three sites");
    const unsigned sub = (elf.word(0) >> 0) & 31;
    Check(sub == 15, "span took x15");
    Check(((elf.word(1) >> 5) & 31) == sub, "pre-index base renamed");
    Check(((elf.word(2) >> 5) & 31) == sub, "follow-up use renamed to the same reg");
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
    test_pressure_outside_span_borrows();
    test_branch_renamed();
    test_branch_bare_skips();
    test_liveout_skips();
    test_callspan_skips();
    test_mem_skips();
    test_personality_skips();
    test_crash_loop_shape();
    test_gap_loop_renamed();
    test_gap_escape_skipped();
    test_crash_shape_renamed();
    test_branch_terminator_spans();
    test_two_spans_one_block();
    test_simd_lane_vs_base();
    test_fmov_crossover();
    test_writeback_preindex();
    if (g_failures == 0) {
        std::printf("=== PASSED (%d checks) ===\n", g_checks);
        return 0;
    }
    std::printf("=== FAILED (%d of %d) ===\n", g_failures, g_checks);
    return 1;
}
