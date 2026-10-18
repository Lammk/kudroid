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

// braa x0, x18: layout is target Rn@9:5, modifier Rm@4:0 (capstone:
// 0xD71F0812). The 0xD7 decoder must rename the modifier; a missed field
// leaves a live x18 that async delivery zeroes.
void test_braa_modifier_renamed() {
    std::printf("[rewrite] braa modifier x18 is renamed\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;  // add x18,x0,x1
    const std::uint32_t braa = 0xD71F0812;  // braa x0, x18
    SynthElf elf({add, braa}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1, "rewritten, def + modifier use");
    Check(((elf.word(0) >> 0) & 31) == 15, "add Rd became x15");
    Check(((elf.word(1) >> 0) & 31) == 15, "braa modifier became x15");
    Check(((elf.word(1) >> 5) & 31) == 0, "braa target untouched");
}

// A call that coincides with the span endpoint is the span's own x18 use
// (blr x18 terminator): the substitute is consumed by the call, not live
// across it, so it rewrites. Strict-interior calls still skip (see above).
void test_callspan_endpoint_rewrites() {
    std::printf("[rewrite] call at the span endpoint rewrites\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;  // add x18,x0,x1
    const std::uint32_t blr = 0xD63F0000 | (18u << 5);  // blr x18
    SynthElf elf({add, blr}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1, "endpoint terminator rewritten");
    Check(((elf.word(0) >> 0) & 31) == 15, "add Rd became x15");
    Check(((elf.word(1) >> 5) & 31) == 15, "blr Rn became x15");
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

// x18 through memory without define: provenance unprovable, skip.
void test_mem_skips() {
    std::printf("[rewrite] x18 memory lane is skipped\n");
    const std::uint32_t str = 0xF9000000 | (18u << 0) | (0u << 5);  // str x18,[x0]
    SynthElf elf({str}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 0, "mem skipped");
    Check(elf.word(0) == str, "bytes untouched");
}

void test_str_x18_renamed() {
    std::printf("[rewrite] str x18 is renamed when defined in function\n");
    const std::uint32_t add = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;  // add x18, x0, x1
    const std::uint32_t str = 0xF9000800 | 18u | (31u << 5);             // str x18, [sp, #16]
    SynthElf elf({add, str}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1, "str rewritten");
    Check(((elf.word(0) >> 0) & 31) == 15, "add Rd became x15");
    Check(((elf.word(1) >> 0) & 31) == 15, "str Rt became x15");
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
        0xD280000F,       // 3: mov x15, #0          (kills incoming x15)
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

// Advanced SIMD structure load/store. Every encoding below was produced by the
// aarch64 assembler (aarch64-linux-gnu-as) and read back with objdump, so the
// constants are what shipped code actually contains -- not hand-assembled bit
// patterns. The family carries one integer register, the base; Rt is a vector
// register and must survive untouched.
//
// Each case opens with "mov x18, x0" because the pass refuses a live-in x18 by
// design (every use must be fed by a define in the same function); a lone
// "ld1 ..., [x18]" would be skipped for that reason and prove nothing about the
// decoder. The crash word itself is reached in real code with exactly such a
// define earlier in the function, and before this arm existed the decode made
// the whole function undecodable anyway.
void test_simd_structure_ldst() {
    std::printf("[rewrite] Advanced SIMD structure load/store renames the base\n");
    const std::uint32_t kDef = 0xAA000012;  // mov x18, x0
    const std::uint32_t kRet = 0xD65F03C0;  // ret

    // ld1 {v18.s}[1], [x18] -- the word from the live crash (libunity+0xcf6140).
    // Rn = x18 and Rt = v18 at the same time: the base must be renamed and the
    // vector lane must not.
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x0D409252,   // 1: ld1 {v18.s}[1], [x18]
            kRet,         // 2: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 2, "ld1 {v18.s}[1],[x18] renamed");
        Check(((elf.word(1) >> 5) & 31) == 15, "structure base x18 became x15");
        Check(((elf.word(1) >> 0) & 31) == 18, "vector lane v18 untouched");
    }

    // st1 {v0.4s}, [x18] -- store form. (An earlier draft of this test used
    // 0x4c007240; the assembler emits 0x4c007a40.)
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x4C007A40,   // 1: st1 {v0.4s}, [x18]
            kRet,         // 2: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 2, "st1 {v0.4s},[x18] renamed");
        Check(((elf.word(1) >> 5) & 31) == 15, "store base x18 became x15");
    }

    // Post-index whose only x18 is Rm: [x0], x18.
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x4CD27800,   // 1: ld1 {v0.4s}, [x0], x18
            kRet,         // 2: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 2, "post-index Rm x18 renamed");
        Check(((elf.word(1) >> 16) & 31) == 15, "post-index Rm became x15");
        Check(((elf.word(1) >> 5) & 31) == 0, "base x0 untouched");
    }

    // Post-index where the base is x18 and Rm is a different register: only the
    // base is a site, and Rm must not be dragged along.
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x4CC17A40,   // 1: ld1 {v0.4s}, [x18], x1
            kRet,         // 2: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 2, "post-index base renamed alone");
        Check(((elf.word(1) >> 5) & 31) == 15, "post-index base became x15");
        Check(((elf.word(1) >> 16) & 31) == 1, "Rm x1 untouched");
    }

    // Both integer registers are x18, in the 0x0C top byte with the post-index
    // bit set -- proof the post-index bit is bit 23 and not the top byte.
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x0CD27640,   // 1: ld1 {v0.4h}, [x18], x18
            kRet,         // 2: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 3, "base and Rm both renamed");
        Check(((elf.word(1) >> 5) & 31) == 15, "base became x15");
        Check(((elf.word(1) >> 16) & 31) == 15, "Rm became the same substitute");
    }

    // Post-index immediate: Rm is pinned to 31 and holds an encoded offset, not
    // a register. Rewriting it would corrupt the addressing, so the field must
    // survive as 31 and count no site.
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x4CDF7800,   // 1: ld1 {v0.4s}, [x0], #16
            kRet,         // 2: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 1,
              "function with a post-index immediate is still rewritten");
        Check(((elf.word(1) >> 16) & 31) == 31,
              "post-index immediate field left as Rm=31, not treated as a register");
    }

    // Multi-register structures and the replicate forms.
    {
        std::vector<std::uint32_t> code = {
            kDef,         // 0: mov x18, x0
            0x4C408A40,   // 1: ld2 {v0.4s-v1.4s}, [x18]
            0x4D40CA40,   // 2: ld1r {v0.4s}, [x18]
            0x4C890A40,   // 3: st4 {v0.4s-v3.4s}, [x18], x9
            kRet,         // 4: ret
        };
        SynthElf elf(code, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 4, "ld2/ld1r/st4 bases renamed");
        Check(((elf.word(1) >> 5) & 31) == 15, "ld2 base became x15");
        Check(((elf.word(2) >> 5) & 31) == 15, "ld1r base became the same substitute");
        Check(((elf.word(3) >> 5) & 31) == 15, "st4 base became the same substitute");
        Check(((elf.word(3) >> 16) & 31) == 9, "st4 Rm x9 untouched");
    }

    // The neighbouring top bytes are not this family. 0x8C is undefined and 0x2C
    // is a non-temporal pair: neither may be absorbed by the new arm. A mask
    // loose enough to cover the family would claim the 0x8C word, and a "known"
    // word is one the pass may then patch.
    {
        const std::uint32_t undefWord = 0x8C407400u | (18u << 5);
        SynthElf undef({undefWord, kRet}, false);
        kudroid::X18Stats stU = undef.run();
        Check(stU.rewritten == 0 && stU.skippedUnknown == 1,
              "undefined 0x8C word with an x18 base still skipped as unknown");
        Check(undef.word(0) == undefWord, "undefined word bytes untouched");

        // ldnp with an x18 base: a real pair, handled by the pre-existing arm
        // the new one sits next to.
        const std::uint32_t ldnp = 0x2C407400u | (18u << 5);
        SynthElf pair({kDef, ldnp, kRet}, false);
        kudroid::X18Stats stP = pair.run();
        Check(stP.rewritten == 1 && stP.sites == 2, "ldnp base still renamed");
        Check(((pair.word(1) >> 5) & 31) == 15, "ldnp base became x15");
    }
}

// LDR defining x18 from stack followed by compare is rewritten.
void test_ldr_x18_renamed() {
    std::printf("[rewrite] ldr x18 destination is renamed\n");
    const std::uint32_t ldr = 0xb94057f2;  // ldr w18, [sp, #0x54]
    const std::uint32_t cmp = 0x6b12013f;  // cmp w9, w18 (subs wzr, w9, w18)
    SynthElf elf({ldr, cmp, 0xD65F03C0}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "ldr x18 rewritten, def + use");
    Check((elf.word(0) & 31) == 15, "ldr target became x15");
    Check(((elf.word(1) >> 16) & 31) == 15, "cmp source became x15");
}

void test_live_in_substitute_preserved() {
    SynthElf elf({0xAA000012, 0x8B120001, 0xAA0F03E2, 0xD65F03C0}, false);
    const auto st = elf.run();
    Check(st.rewritten == 1, "live-in substitute case rewrites");
    Check((elf.word(0) & 31) != 15, "incoming x15 is not overwritten");

    SynthElf target({0xAA000012, 0x8B120001, 0xD63F01E0, 0xD65F03C0}, false);
    target.run();
    Check((target.word(0) & 31) != 15, "indirect call target x15 is not overwritten");
}

void test_ldp_stp_signed_offset() {
    std::printf("[rewrite] ldp/stp signed offset with x18 base and target is rewritten\n");
    const std::uint32_t def_x18 = 0x8B010000 | (1u << 16) | (0u << 5) | 18u;  // add x18, x0, x1
    const std::uint32_t stp_base = 0xA9010640;                                // stp x0, x1, [x18, #16]
    SynthElf elf({def_x18, stp_base, 0xD65F03C0}, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "stp signed offset base rewritten");
    Check(((elf.word(1) >> 5) & 31) == 15, "stp Rn became x15");

    const std::uint32_t ldp_def = 0xA9410BE0 | 18u;  // ldp x18, x2, [sp, #16]
    const std::uint32_t use_x18 = 0x8B120000;        // add x0, x0, x18
    SynthElf elf2({ldp_def, use_x18, 0xD65F03C0}, false);
    kudroid::X18Stats st2 = elf2.run();
    Check(st2.rewritten == 1 && st2.sites == 2, "ldp signed offset target rewritten");
    Check(((elf2.word(0) >> 0) & 31) == 15, "ldp Rt became x15");
    Check(((elf2.word(1) >> 16) & 31) == 15, "add Rm became x15");
}

void test_x30_fallback_picked() {
    std::printf("[rewrite] x30 picked as fallback when x0-x15 busy before call\n");
    std::vector<std::uint32_t> code;
    code.push_back(0x8B010000 | (1u << 16) | (0u << 5) | 18u);  // add x18, x0, x1 (def)
    for (unsigned r = 0; r <= 15; ++r) {
        // add x_r, x_r, #0 — every caller-saved register busy inside the span
        code.push_back(0x91000000 | (r << 0) | (r << 5));
    }
    code.push_back(0x8B020240);  // add x0, x18, x2 (use)
    code.push_back(0x94000001);  // bl #4 (call clobbers x30 and caller-saved)
    code.push_back(0xD65F03C0);  // ret
    SynthElf elf(code, false);
    kudroid::X18Stats st = elf.run();
    Check(st.rewritten == 1 && st.sites == 2, "rewritten using x30 fallback");
    Check(((elf.word(0) >> 0) & 31) == 30, "span picked x30 (Rd)");
    Check(((elf.word(17) >> 5) & 31) == 30, "span picked x30 (Rn)");
}

void test_layer2_virtual_x18() {
    std::printf("[rewrite] layer 2 virtual x18 getter/setter\n");
    kudroid::elf_x18_set(0x12345678deadbeefull);
    Check(kudroid::elf_x18_get() == 0x12345678deadbeefull, "vx18 set and read back correctly");
    kudroid::elf_x18_set(0);
    Check(kudroid::elf_x18_get() == 0, "vx18 cleared");
}

// A slice of a function (gap path) that branches into the next slice hands it
// every register: x15 is read there, so it must not be the substitute here.
void test_window_exit_keeps_registers_live() {
    std::printf("[rewrite] window exit keeps registers live\n");
    const std::uint32_t kAddX18 = 0x8B1001D2u;  // add x18, x14, x16
    std::vector<std::uint32_t> code = {
        kAddX18,      // 0: add x18, x14, x16
        0xEB12023Fu,  // 1: cmp x17, x18
        0x54000062u,  // 2: b.cs -> 5 (next slice)
        0xAA1F03F0u,  // 3: mov x16, xzr
        0x14000003u,  // 4: b -> 7 (ends the slice)
        0x927DF1F0u,  // 5: and x16, x15, #~7 (reads x15)
        0xAA1003F2u,  // 6: mov x18, x16
        0xD65F03C0u,  // 7: ret
    };
    SynthElf elf(code, false, 0);
    elf.run();
    Check(((elf.word(0) >> 0) & 31) != 15, "x15 live into the next slice is not overwritten");
}

// Vector by-element / modified-immediate forms carry v18 or immediate bits in the
// field a scalar slot scan reads as x18. They used to make the whole function
// "unknown" and skip it, leaving its real x18 uses exposed to async clobbering.
void test_simd_immediate_forms_do_not_block_rewrite() {
    std::printf("[rewrite] SIMD by-element and immediate forms do not block the rewrite\n");
    const std::uint32_t kFmulElem = 0x4FA79247u;  // fmul v7.4s, v18.4s, v7.s[1]
    const std::uint32_t kMvni = 0x6F000642u;      // mvni v2.4s, #0x12
    const std::uint32_t kMovi = 0x4F042412u;      // movi v18.4s, #0x80, lsl #8
    const std::uint32_t kShl = 0x4F0C5612u;       // shl v18.16b, v16.16b, #4
    SynthElf elf({kFmulElem, kMvni, kMovi, kShl,
                  0x91001012u,   // add x18, x0, #4
                  0xF9400241u,   // ldr x1, [x18]
                  0xD65F03C0u},  // ret
                 false);
    const auto st = elf.run();
    Check(st.rewritten == 1, "the function is rewritten, not skipped as unknown");
    Check(elf.word(0) == kFmulElem && elf.word(1) == kMvni && elf.word(2) == kMovi &&
              elf.word(3) == kShl,
          "vector words are untouched");
    Check((elf.word(4) & 31u) != 18u && ((elf.word(5) >> 5) & 31u) != 18u,
          "the scalar x18 define and use are renamed");
}

// madd/msub name a real accumulator register in bits[14:10]. Treating it as an
// opaque slot made any function with `madd ..., x18` impossible to rewrite or stub.
void test_multiply_add_accumulator_renamed() {
    std::printf("[rewrite] madd accumulator x18 is renamed with its definition\n");
    SynthElf elf({0xAA0003F2u,   // mov x18, x0
                  0x9B034841u,   // madd x1, x2, x3, x18
                  0xD65F03C0u},  // ret
                 false);
    const auto st = elf.run();
    Check(st.rewritten == 1, "the function is rewritten");
    const unsigned def = elf.word(0) & 31u;
    const unsigned acc = (elf.word(1) >> 10) & 31u;
    Check(def != 18u && acc == def, "definition and accumulator share the replacement");
}

// A routine with no prologue that follows a `ret` behind an `adrp` is not split off by
// the prologue scan, so its words look unreachable from the chunk start. Its x18 uses
// must still be rewritten, not abandoned as "data".
void test_leaf_routine_after_adrp_is_split_off() {
    std::printf("[rewrite] a prologue-less routine behind an adrp is rewritten\n");
    SynthElf elf({0xD2800020u,   // mov x0, #1
                  0xD65F03C0u,   // ret
                  0x90000011u,   // adrp x17, #0
                  0xAA0103F2u,   // mov x18, x1
                  0x91001240u,   // add x0, x18, #4
                  0xD65F03C0u},  // ret
                 false, 0);      // no FDE: gap path
    const auto st = elf.run();
    Check(st.rewritten == 1, "the second routine is rewritten");
    Check((elf.word(3) & 31u) != 18u && ((elf.word(4) >> 5) & 31u) != 18u,
          "its x18 define and use are renamed");
}

// A tail call hands the callee this function's return address, so x30 is live up to it. With
// every other scratch register busy, the allocator's last resort used to be x30: the
// function then tail-called with a data value in x30 and the callee "returned" into it.
void test_tail_call_keeps_return_address_register() {
    std::printf("[rewrite] x30 is not borrowed in a function that ends in a tail call\n");
    std::vector<std::uint32_t> code;
    code.push_back(0x8B010000u | (1u << 16) | (0u << 5) | 18u);  // add x18, x0, x1
    for (unsigned r = 0; r <= 15; ++r) {
        code.push_back(0x91000000u | (r << 5) | r);  // add xN, xN, #0 (every candidate busy)
    }
    code.push_back(0x91001240u);  // add x0, x18, #4 (the use)
    code.push_back(0x14000100u);  // b +0x400: out of the function, a tail call
    const std::uint32_t def = code[0];
    SynthElf elf(code, false);
    const auto st = elf.run();
    Check(elf.word(0) != (def & ~31u | 30u), "x30 did not become the replacement");
    Check(st.rewritten == 0 && elf.word(0) == def,
          "the function is left for the virtual-x18 path, bytes untouched");
}

void test_loop_preserves_live_mask() {
    std::printf("[rewrite] backward edges preserve live replacement candidates\n");
    std::vector<std::uint32_t> code;
    for (unsigned r = 0; r < 15; ++r) {
        code.push_back(0x91000000u | (r << 5) | r);  // add xN, xN, #0
    }
    code.insert(code.end(), {
        0xD28000EFu,  // mov x15, #7
        0xD2800042u,  // mov x2, #2
        0x8A0F0021u,  // and x1, x1, x15
        0xAA000012u,  // mov x18, x0
        0x91000640u,  // add x0, x18, #1
        0xF1000442u,  // subs x2, x2, #1
        0x54FFFF81u,  // b.ne -16
        0xD65F03C0u,  // ret
    });
    SynthElf elf(code, false);
    const auto st = elf.run();
    Check(st.rewritten == 1, "loop has a safe replacement");
    Check((elf.word(18) & 31u) != 15,
          "temporary does not overwrite the mask used on the next iteration");
    Check((elf.word(18) & 31u) == ((elf.word(19) >> 5) & 31u),
          "definition and use share a replacement");
}

void test_branch_definitions_share_replacement() {
    std::printf("[rewrite] joined definitions share one replacement\n");
    SynthElf elf({
        0xB4000060u,  // cbz x0, +12
        0xAA0103F2u,  // mov x18, x1
        0x14000002u,  // b +8
        0xAA0203F2u,  // mov x18, x2
        0x8B030240u,  // add x0, x18, x3
        0xD65F03C0u,  // ret
    }, false);
    const auto st = elf.run();
    Check(st.rewritten == 1, "both branch definitions can be renamed");
    const unsigned replacement = (elf.word(4) >> 5) & 31u;
    Check(replacement != 18 && (elf.word(1) & 31u) == replacement &&
          (elf.word(3) & 31u) == replacement,
          "either branch supplies the register read at the join");
}

// Words whose Rd/Rn/Rm field holds 18 but names a vector register, an FP register or
// an immediate/offset field. Decoding any of them as x18 renames bits that are not a
// register and corrupts the code (a constant 32-sample decoder window, float struct
// loads at large offsets). Every word must come through byte-identical.
void test_non_gpr_18_untouched() {
    std::printf("[rewrite] FP/SIMD/offset fields equal to 18 are not x18\n");
    const std::uint32_t words[] = {
        0x1E273A52,  // fsub s18, s18, s7   (Rm=7 matches the old scvtf/fmov "kind")
        0x1E263A52,  // fsub s18, s18, s6   (Rm=6)
        0x1E363A52,  // fsub s18, s18, s22  (Rm=22)
        0x1E222A44,  // fadd s4, s18, s2    (Rm=2)
        0x1E383872,  // fsub s18, s3, s24   (Rm=24)
        0x6E21DE58,  // fmul v24.4s, v18.4s, v1.4s (vector arithmetic, top byte 0x6E)
        0x6E124252,  // ext v18.16b, v18.16b, v18.16b, #8
        0x6E180643,  // mov v3.d[1], v18.d[0]      (INS element, bit 29 set)
        0x2EB11E43,  // bit v3.8b, v18.8b, v17.8b
        0x7E21DA52,  // ucvtf s18, s18             (Advanced SIMD scalar)
        0x1E622250,  // fcmpe d18, d2
        0x9EAE0240,  // fmov x0, v18.d[1]
        0x9EAF0012,  // fmov v18.d[1], x0
        0x4E040C32,  // dup v18.4s, w1
        0xAD404C12,  // ldp q18, q19, [x0]
        0x3C9203A1,  // stur q1, [x29, #-224]  (imm9 bits[20:16] == 18)
        0x3CD203A0,  // ldur q0, [x29, #-224]
        0xF8520281,  // ldur x1, [x20, #-224]
        0xBD72BC04,  // ldr s4, [x0, #12988]   (imm12 bits[20:16] == 18)
        0xBD328281,  // str s1, [x20, #12928]
        0x39722021,  // ldrb w1, [x1, #3208]
        0xFA520800,  // ccmp x0, #18, #0, eq   (imm5, not Rm)
        0xBA521800,  // ccmn x0, #18, #0, ne
    };
    for (std::uint32_t w : words) {
        SynthElf elf({w, 0xD65F03C0}, false);
        kudroid::X18Stats st = elf.run();
        char what[64];
        std::snprintf(what, sizeof(what), "0x%08X left untouched", static_cast<unsigned>(w));
        Check(st.rewritten == 0 && st.sites == 0 && st.stubbedSites == 0 &&
                  st.skippedUnknown == 0 && elf.word(0) == w,
              what);
    }
}

// The same families where x18 IS a general register: SIMD<->GPR moves, fixed-point
// converts, flag-setting carry/compare and LSE atomics. A def-only word is renamed in
// place; a use needs a preceding def so x18 is not live-in.
void test_gpr_18_in_simd_fp_and_flags() {
    std::printf("[rewrite] x18 in SIMD<->GPR, fixed-point, ccmp and LSE forms renames\n");
    struct Def { std::uint32_t w; const char* name; };
    const Def defs[] = {
        {0x4E183C72, "umov x18, v3.d[1]"},
        {0x4E0C2C32, "smov x18, v1.s[1]"},
        {0x0E073C12, "umov w18, v0.b[3]"},
        {0x0E0A2C12, "smov w18, v0.h[2]"},
        {0x1E18F412, "fcvtzs w18, s0, #3"},
        {0x9E59E452, "fcvtzu x18, d2, #7"},
    };
    for (const Def& c : defs) {
        SynthElf elf({c.w, 0xD65F03C0}, false);
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 1 && ((elf.word(0) >> 0) & 31) == 15, c.name);
    }
    struct Use { std::uint32_t w; unsigned field; const char* name; };
    const Use uses[] = {
        {0x4E040E40, 5, "dup v0.4s, w18"},
        {0x4E0C1E40, 5, "ins v0.s[1], w18"},
        {0x9E02F640, 5, "scvtf s0, x18, #3"},
        {0x1E43EE41, 5, "ucvtf d1, w18, #5"},
        {0xFA410240, 5, "ccmp x18, x1, #0, eq"},
        {0xFA520020, 16, "ccmp x1, x18, #0, eq"},
        {0xBA020240, 5, "adcs x0, x18, x2"},
        {0xF8320041, 16, "ldadd x18, x1, [x2]"},
    };
    for (const Use& c : uses) {
        SynthElf elf({0xAA000012, c.w, 0xD65F03C0}, false);  // mov x18, x0 first
        kudroid::X18Stats st = elf.run();
        Check(st.rewritten == 1 && st.sites == 2 && ((elf.word(1) >> c.field) & 31) == 15,
              c.name);
    }
}

// A literal pool or an embedded blob in uncovered code is data. Float constants such as
// 420.0f (0x43D20000) have bits[27:25] == 001 and bits[20:16] == 18; decoding them as
// bitfield/extract instructions made the gap pass rewrite them into branches.
void test_gap_data_words_untouched() {
    std::printf("[rewrite] data words in an uncovered run are not rewritten\n");
    const std::uint32_t data[] = {
        0x43D20000, 0x43B20000, 0x43928000, 0x43F28000,
        0x43D20000, 0x43B20000, 0x43928000, 0x43F28000,
    };
    std::vector<std::uint32_t> code = {0xAA0103E0, 0xD65F03C0};  // mov x0, x1; ret
    for (std::uint32_t w : data) code.push_back(w);
    SynthElf elf(code, false, 8);  // FDE covers only the two instructions
    kudroid::X18Stats st = elf.run();
    bool intact = true;
    for (std::size_t i = 0; i < code.size(); ++i) intact = intact && elf.word(i) == code[i];
    Check(intact, "every word is byte-identical");
    Check(st.rewritten == 0 && st.sites == 0 && st.stubbedSites == 0, "nothing rewritten or stubbed");

    // The same constants reached from covered code (a pool inside a function body).
    for (std::uint32_t w : data) {
        SynthElf pool({w, 0xD65F03C0}, false);
        kudroid::X18Stats ps = pool.run();
        char what[64];
        std::snprintf(what, sizeof(what), "pool word 0x%08X untouched", static_cast<unsigned>(w));
        Check(pool.word(0) == w && ps.stubbedSites == 0 && ps.sites == 0, what);
    }
}

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
    test_braa_modifier_renamed();
    test_branch_bare_skips();
    test_liveout_skips();
    test_callspan_skips();
    test_callspan_endpoint_rewrites();
    test_mem_skips();
    test_str_x18_renamed();
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
    test_simd_structure_ldst();
    test_ldr_x18_renamed();
    test_live_in_substitute_preserved();
    test_ldp_stp_signed_offset();
    test_x30_fallback_picked();
    test_layer2_virtual_x18();
    test_loop_preserves_live_mask();
    test_multiply_add_accumulator_renamed();
    test_leaf_routine_after_adrp_is_split_off();
    test_tail_call_keeps_return_address_register();
    test_simd_immediate_forms_do_not_block_rewrite();
    test_window_exit_keeps_registers_live();
    test_branch_definitions_share_replacement();
    test_non_gpr_18_untouched();
    test_gpr_18_in_simd_fp_and_flags();
    test_gap_data_words_untouched();
    if (g_failures == 0) {
        std::printf("=== PASSED (%d checks) ===\n", g_checks);
        return 0;
    }
    std::printf("=== FAILED (%d of %d) ===\n", g_failures, g_checks);
    return 1;
}
