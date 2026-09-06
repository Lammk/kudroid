// AOT x18 rewrite (see ElfX18.h for the soundness rules).
#include "kudroid/ElfX18.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace kudroid {
namespace x18 {
namespace {

constexpr unsigned kX18 = 18;

struct Decoded {
    std::uint32_t used = 0;  // integer regs touched (bit r = x_r, r < 31)
    // Patchable 5-bit field offsets holding a register number.
    std::uint8_t at[4] = {0, 0, 0, 0};
    std::uint8_t npatch = 0;
    std::uint8_t defmask = 0;  // bit k set when at[k] defines (writes)
    // Positions known to hold a register but never patched (opaque to us).
    std::uint8_t useonly[4] = {0, 0, 0, 0};
    std::uint8_t nuse = 0;
    bool mem18 = false;  // x18 crosses memory: unrewritable, skip
    bool known = true;  // false: encoding not recognized
};

void markUsed(Decoded& d, unsigned reg) {
    if (reg < 31) d.used |= 1u << reg;
}

void markPatch(Decoded& d, unsigned bitoff, unsigned reg, bool isDef) {
    markUsed(d, reg);
    if (d.npatch < 4) {
        d.at[d.npatch] = static_cast<std::uint8_t>(bitoff);
        if (isDef) d.defmask |= static_cast<std::uint8_t>(1u << d.npatch);
        ++d.npatch;
    }
}

void markUseOnly(Decoded& d, unsigned bitoff, unsigned reg) {
    markUsed(d, reg);
    if (d.nuse < 4) d.useonly[d.nuse++] = static_cast<std::uint8_t>(bitoff);
}

// Memory lane holding x18: the value crosses memory, so no in-chunk rename
// can stay consistent with whoever wrote or will read it.
void markMem(Decoded& d, unsigned, unsigned reg) {
    markUsed(d, reg);
    if (reg == kX18) d.mem18 = true;
}

// Decode one instruction for integer-register use. Patch positions are only
// recorded where the field is CERTAIN to be a register; anywhere else a value
// of 18 forces the whole function to be skipped.
Decoded decode(std::uint32_t w) {
    Decoded d;
    const unsigned b = w >> 24;
    const unsigned rd = (w >> 0) & 31;
    const unsigned rn = (w >> 5) & 31;
    const unsigned rm = (w >> 16) & 31;
    const unsigned ra = (w >> 10) & 31;

    // Branches with register: br/blr/ret + authenticated variants (read).
    if (b == 0xD6) {
        markPatch(d, 5, rn, false);
        return d;
    }
    // b/bl span four top bytes each (condition/offset bits, no registers).
    if ((b & 0xFC) == 0x14 || (b & 0xFC) == 0x94) return d;
    // cbz/cbnz (test only).
    if ((b & 0x7F) == 0x34 || (b & 0x7F) == 0x35) {
        markPatch(d, 0, rd, false);
        return d;
    }
    // b.cond has a condition code, not Rt, in [4:0].
    if (b == 0x54) return d;
    // tbz/tbnz (test only).
    if ((b & 0x7E) == 0x36) {
        markPatch(d, 0, rd, false);
        return d;
    }
    // adr/adrp (define).
    if ((b & 0x1F) == 0x10) {
        markPatch(d, 0, rd, true);
        return d;
    }
    // add/sub immediate (+flags): Rd defines, Rn reads.
    if (b == 0x11 || b == 0x31 || b == 0x51 || b == 0x71 || b == 0x91 || b == 0xB1 ||
        b == 0xD1 || b == 0xF1) {
        markPatch(d, 0, rd, true);
        markPatch(d, 5, rn, false);
        return d;
    }
    // Data-processing immediate group needs the op field.
    if (((w >> 25) & 7) == 1) {
        const unsigned op = (w >> 23) & 3;
        if (op == 1) {
            markPatch(d, 0, rd, true);  // mov wide: Rd only
        } else if (op == 3) {
            markPatch(d, 0, rd, true);  // extr
            markPatch(d, 5, rn, false);
            markPatch(d, 16, rm, false);
        } else {
            markPatch(d, 0, rd, true);  // logical-imm / bitfield: Rd + Rn
            markPatch(d, 5, rn, false);
        }
        return d;
    }
    // Logical / add-sub register (+shifted/extended).
    auto isLogicReg = [&] {
        return b == 0x0A || b == 0x2A || b == 0x4A || b == 0x6A || b == 0x8A ||
               b == 0xAA || b == 0xCA || b == 0xEA || b == 0x0B || b == 0x2B ||
               b == 0x4B || b == 0x6B || b == 0x8B || b == 0xAB || b == 0xCB ||
               b == 0xEB;
    };
    if (isLogicReg()) {
        markPatch(d, 0, rd, true);
        markPatch(d, 5, rn, false);
        markPatch(d, 16, rm, false);
        return d;
    }
    // Conditional select / multiply: Ra field is real, except fixed patterns
    // (sdiv Ra=11111) which never equal 18 and are safe to leave marked.
    if (b == 0x1A || b == 0x5A || b == 0x9A || b == 0xDA || b == 0x1B || b == 0x9B) {
        markPatch(d, 0, rd, true);
        markPatch(d, 5, rn, false);
        markPatch(d, 16, rm, false);
        markUseOnly(d, 10, ra);
        return d;
    }
    // Loads/stores, single (unsigned/unscaled/pre/post/reg-offset/prfm/LSE).
    if ((b & 0x3B) == 0x38 || (b & 0x3B) == 0x39 || (b & 0x3B) == 0x3A) {
        const bool simd = (w & (1u << 26)) != 0;
        // PRFM immediate: bits[31:23] fixed, Rn base, no Rt destination.
        if (((w >> 23) & 0x1FF) == 0x1F3) {
            markPatch(d, 5, rn, false);
            return d;
        }
        // LSE atomics share the unscaled top bytes with opc != 0 and carry an
        // Rs source the plain form lacks (Rs reads, Rt defines, Rn reads).
        if ((b & 0x3B) == 0x38 && ((w >> 22) & 3) != 0) {
            markPatch(d, 16, rm, false);
        }
        // Destination-ness by class: unsigned/reg-offset bit22 (STR 0/LDR 1),
        // unscaled bit23 (STUR 0/LDUR 1). Verified against shipped bytes.
        bool isLoad = false;
        if ((b & 0x3B) == 0x38)
            isLoad = ((w >> 23) & 1) != 0;
        else
            isLoad = ((w >> 22) & 1) != 0;
        if (!simd) markMem(d, 0, rd);
        markPatch(d, 5, rn, false);
        if ((b & 0x3B) == 0x3A) markPatch(d, 16, rm, false);
        return d;
    }
    // Loads/stores, pair. Integer lanes share top bytes with SIMD lanes;
    // only Rn is an integer register in the SIMD form. Store forms (opc
    // bit22 clear) read their lanes; load forms define them.
    if ((b & 0x3F) == 0x28) {
        markMem(d, 0, rd);
        markMem(d, 10, ra);
        markPatch(d, 5, rn, false);
        return d;
    }
    if ((b & 0x3C) == 0x2C) {
        markPatch(d, 5, rn, false);
        return d;
    }
    // System class: mrs defines Rt (bits[21:20] == 3, verified against the
    // TPIDR read shape); msr/sys read it; hints carry 11111 (inert).
    if (b == 0xD5) {
        markPatch(d, 0, rd, ((w >> 20) & 3) == 3);
        return d;
    }
    // SVC/HVC/SMC/ERET/BRK/HLT/DCPS: immediates only.
    if (b == 0xD4) return d;
    // FP/SIMD space is deliberately NOT decoded: scalar-FP and vector ops
    // share prefixes/bits with integer-side moves (fmov/converts), so no
    // bit pattern proves which side a register field belongs to. Everything here
    // falls into the unknown bucket below (slot scan, skip-if-18, never patch).
    d.known = false;
    if (rd == kX18 || rn == kX18 || rm == kX18 || ra == kX18) d.used |= 1u << kX18;
    return d;
}

// Standard integer-reg slot scan for unrecognized encodings.
bool maybeX18(std::uint32_t w) {
    return ((w >> 0) & 31) == kX18 || ((w >> 5) & 31) == kX18 ||
           ((w >> 16) & 31) == kX18 || ((w >> 10) & 31) == kX18;
}

// ---- .eh_frame walk ------------------------------------------------------
// Minimal reader: CIE augmentation (personality 'P'?) + FDE pc ranges.
// Anything undecodable skips the single record, never the pass.
struct FdeRange {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    bool personality = false;
};

std::uint64_t readUleb(const std::uint8_t* p, const std::uint8_t* end, size_t& n) {
    std::uint64_t r = 0;
    unsigned s = 0;
    n = 0;
    while (p + n < end) {
        const unsigned b = p[n++];
        r |= static_cast<std::uint64_t>(b & 0x7F) << s;
        s += 7;
        if ((b & 0x80) == 0) break;
    }
    return r;
}

// Length field of an FDE range: address-sized for the encoding family.
// pcrel+sdata4 (0x1B, the Android arm64 standard) is a 4-byte signed length.
bool readRangeLen(const std::uint8_t* p, const std::uint8_t* end, unsigned enc,
                  std::uint64_t& out, size_t& n) {
    n = 0;
    if (enc == 0x1B) {
        if (p + 4 > end) return false;
        std::int32_t v = 0;
        std::memcpy(&v, p, 4);
        out = static_cast<std::uint64_t>(v);
        n = 4;
        return true;
    }
    const unsigned fmt = enc & 0x0F;
    if (fmt == 0x0B) {  // uleb128
        out = readUleb(p, end, n);
        return n > 0;
    }
    unsigned bytes = 0;
    if (fmt == 0x00 || fmt == 0x04 || fmt == 0x03) {
        bytes = 8;
    } else if (fmt == 0x0A) {
        bytes = 4;
    } else if (fmt == 0x02) {
        bytes = 2;
    } else {
        return false;
    }
    if (p + bytes > end) return false;
    out = 0;
    std::memcpy(&out, p, bytes);
    n = bytes;
    return true;
}

bool readEncoded(const std::uint8_t* p, const std::uint8_t* end, unsigned enc,
                 std::uint64_t& out, size_t& n) {
    n = 0;
    if (enc == 0x1B) {  // pcrel + sdata4: 4-byte signed (caller adds the base)
        if (p + 4 > end) return false;
        std::int32_t v = 0;
        std::memcpy(&v, p, 4);
        out = static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
        n = 4;
        return true;
    }
    const unsigned fmt = enc & 0x0F;
    if (fmt == 0x00 || fmt == 0x04) {  // absptr (assume 8 bytes on arm64)
        if (p + 8 > end) return false;
        std::memcpy(&out, p, 8);
        n = 8;
        return true;
    }
    if (fmt == 0x0B) {  // uleb128
        out = readUleb(p, end, n);
        return n > 0;
    }
    if (fmt == 0x0A) {  // udata4
        if (p + 4 > end) return false;
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        out = v;
        n = 4;
        return true;
    }
    if (fmt == 0x02) {  // udata2
        if (p + 2 > end) return false;
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        out = v;
        n = 2;
        return true;
    }
    return false;
}

void walkEhFrame(const std::uint8_t* eh, std::size_t ehLen, std::vector<FdeRange>& out) {
    const std::uint8_t* end = eh + ehLen;
    struct CieInfo {
        std::size_t off = 0;
        bool personality = false;
        unsigned rangeEnc = 0x1B;
    };
    std::vector<CieInfo> cies;
    const std::uint8_t* p = eh;
    while (p + 4 <= end) {
        std::uint32_t len;
        std::memcpy(&len, p, 4);
        if (len == 0) break;
        const std::uint8_t* rec = nullptr;
        const std::uint8_t* recEnd = nullptr;
        if (len == 0xFFFFFFFFu) {
            // 64-bit DWARF length: not expected, but skip cleanly.
            if (p + 12 > end) break;
            std::uint64_t len64 = 0;
            std::memcpy(&len64, p + 4, 8);
            rec = p + 12;
            if (len64 > static_cast<std::uint64_t>(end - rec)) break;
            recEnd = rec + static_cast<std::size_t>(len64);
        } else {
            rec = p + 4;
            recEnd = rec + len;
            if (recEnd > end) break;
        }
        std::uint32_t cid;
        std::memcpy(&cid, rec, 4);
        if (cid == 0) {
            // CIE: version, augmentation, code/data factors, return column,
            // optional augmentation data (z...).
            const std::uint8_t* o = rec + 4 + 1;
            const std::uint8_t* z = o;
            while (o < recEnd && *o != '\0') ++o;
            std::string aug(reinterpret_cast<const char*>(z),
                            static_cast<size_t>(o - z));
            ++o;  // NUL
            size_t n = 0;
            readUleb(o, recEnd, n);
            o += n;  // code alignment
            readUleb(o, recEnd, n);
            o += n;  // data alignment (signedness irrelevant to length)
            readUleb(o, recEnd, n);
            o += n;  // return column
            bool hasP = false;
            unsigned rangeEnc = 0x1B;
            const size_t zpos = aug.find('z');
            if (zpos != std::string::npos) {
                size_t alen = 0;
                const std::uint64_t al = readUleb(o, recEnd, alen);
                o += alen;
                const std::uint8_t* ae = o;
                for (size_t i = zpos + 1; i < aug.size() && o < recEnd;) {
                    const char ch = aug[i++];
                    if (ch == 'P') {
                        hasP = true;
                        const unsigned penc = *o++;
                        std::uint64_t dummy = 0;
                        size_t dn = 0;
                        if (!readEncoded(o, recEnd, penc, dummy, dn)) break;
                        o += dn;
                    } else if (ch == 'L') {
                        ++o;
                    } else if (ch == 'R') {
                        rangeEnc = *o++;
                    } else {
                        break;
                    }
                }
                (void)al;
                (void)ae;
            }
            cies.push_back({static_cast<std::size_t>(rec - 4 - eh), hasP, rangeEnc});
        } else {
            // FDE: CIE pointer is (field address - cid).
            const std::size_t cieOff = static_cast<std::size_t>((rec - eh)) - cid;
            bool hasP = false;
            unsigned rangeEnc = 0x1B;
            for (const auto& c : cies) {
                if (c.off == cieOff) {
                    hasP = c.personality;
                    rangeEnc = c.rangeEnc;
                    break;
                }
            }
            const std::uint8_t* o = rec + 4;
            std::uint64_t loc = 0, rng = 0;
            size_t n = 0;
            const bool isPcrel = (rangeEnc & 0x70) == 0x10;
            if (!readEncoded(o, recEnd, rangeEnc, loc, n)) {
                p = recEnd;
                continue;
            }
            const std::uint8_t* field = o;
            o += n;
            if (isPcrel) loc += reinterpret_cast<std::uint64_t>(field);
            if (!readRangeLen(o, recEnd, rangeEnc, rng, n)) {
                p = recEnd;
                continue;
            }
            if (rng > 0 && rng < 0x1000000) {
                out.push_back({loc, loc + rng, hasP});
            }
        }
        p = recEnd;
    }
}

// ---- gap discovery -------------------------------------------------------
// FDE-less code (hand-tuned DSP compiled without unwind tables) still needs
// entries. Candidates: prologue shapes, thunk branches, and fallthrough
// after terminal control flow. Every candidate is validated by code-likeness
// (pools fail it) and by the span rules downstream.
bool isEntryWord(std::uint32_t w) {
    const unsigned b = w >> 24;
    if ((w & 0xFFC003E0) == 0xA98003E0) return true;  // stp *,*,[sp,#-x] (spill)
    if ((w & 0xFF0003FF) == 0xD10003FF) return true;  // sub sp,sp,#x
    if ((w & 0xFFC003E0) == 0xF81003E0) return true;  // str *,[sp,#-x]
    if (w == 0x910003FD) return true;                 // mov x29,sp
    if (b == 0x90 || b == 0xB0 || b == 0xD0 || b == 0xF0) return true;  // adrp
    if ((b & 0xFC) == 0x14) return true;              // b (thunk)
    if (w == 0xD503237F || w == 0xD503245F) return true;  // pacibsp / bti c
    return false;
}

bool isTerminal(std::uint32_t w) {
    const unsigned b = w >> 24;
    if (b == 0xD6) return true;  // br/blr/ret/eret
    if ((b & 0xFC) == 0x14) return true;  // b (no fallthrough)
    return false;
}

// Fraction of known-class words in the first few: real code scores high,
// pools (addresses/floats/offsets) do not.
bool looksLikeCode(const std::uint32_t* code, std::size_t nwords) {
    std::size_t n = nwords < 8 ? nwords : 8;
    if (n == 0) return false;
    std::size_t known = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (decode(code[i]).known) ++known;
    }
    return known * 4 >= n * 3;
}

// Caller-saved integer candidates (x16/x17 excluded: linker veneers;
// x29/x30/SP excluded structurally).
unsigned pickFreeReg(std::uint32_t used) {
    static const unsigned kOrder[] = {15, 14, 13, 12, 11, 10, 9, 8,
                                      7,  6,  5,  4,  3,  2,  1,  0};
    for (unsigned r : kOrder) {
        if ((used & (1u << r)) == 0) return r;
    }
    return 31;
}

bool hasCall(std::uint32_t w) {
    const unsigned b = w >> 24;
    if ((b & 0xFC) == 0x94 || b == 0xD6) return true;  // bl / blr(+auth)
    return false;
}

// One word's x18 touch: define kills the incoming value, use reads it.
// Memory traffic and unrecognized-but-x18-shaped words count as use.
struct Touch {
    bool known = true;
    bool use = false;
    bool def = false;
};

Touch touch18(std::uint32_t w) {
    Touch t;
    const Decoded d = decode(w);
    t.known = d.known;
    if (!d.known) {
        t.use = ((w >> 0) & 31) == kX18 || ((w >> 5) & 31) == kX18 ||
                ((w >> 16) & 31) == kX18 || ((w >> 10) & 31) == kX18;
        return t;
    }
    if (d.mem18) {
        t.use = true;
        return t;
    }
    for (unsigned k = 0; k < d.npatch; ++k) {
        if (((w >> d.at[k]) & 31) == kX18)
            (((d.defmask >> k) & 1) ? t.def : t.use) = true;
    }
    for (unsigned k = 0; k < d.nuse; ++k) {
        if (((w >> d.useonly[k]) & 31) == kX18) t.use = true;
    }
    if ((d.used & (1u << kX18)) && !t.def && !t.use) t.use = true;
    return t;
}

// Successor walk for exit liveness. complete = whole function (FDE path):
// exits die per NDK convention (return/tail-call kill scratch). Otherwise
// (gap path) only ret/calls and jumps into FDE-covered functions die;
// anything else leaving the window escapes.
enum class Edge { Next, Dies, Escapes };

bool coveredContains(
    const std::vector<std::pair<std::size_t, std::size_t>>& covered,
    std::size_t t) {
    std::size_t lo = 0, hi = covered.size();
    while (lo < hi) {
        const std::size_t m = lo + (hi - lo) / 2;
        if (covered[m].first <= t)
            lo = m + 1;
        else
            hi = m;
    }
    return lo > 0 && covered[lo - 1].second > t;
}

Edge succEdge(const std::uint32_t* win, std::size_t n, std::size_t i,
              std::vector<std::size_t>& out, bool complete,
              const std::vector<std::pair<std::size_t, std::size_t>>* covered) {
    const std::uint32_t w = win[i];
    const unsigned b = w >> 24;
    auto target = [&](long t) {
        if (t >= 0 && static_cast<std::size_t>(t) < n) {
            out.push_back(static_cast<std::size_t>(t));
            return Edge::Next;
        }
        if (complete) return Edge::Dies;
        if (covered != nullptr && t >= 0 &&
            coveredContains(*covered, static_cast<std::size_t>(t)))
            return Edge::Dies;  // tail-call into a known function
        return Edge::Escapes;
    };
    if (b == 0xD6) {
        // ret dies everywhere; blr is a call (scratch dies per NDK
        // convention); br/eret jump unknown: tail-call dies in a whole
        // function, but mid-gap they may land in unrenamed code.
        if ((w & ~0x3E0u) == 0xD65F03C0u) return Edge::Dies;  // ret
        if (((w >> 16) & 0xFF) == 0x3F) return Edge::Dies;    // blr(+auth)
        if (((w >> 5) & 31) == 30) return Edge::Dies;  // br x30: return
        return complete ? Edge::Dies : Edge::Escapes;  // br/eret
    }
    if ((b & 0xFC) == 0x94) return Edge::Dies;  // bl: scratch dies at call
    if ((b & 0xFC) == 0x14) {
        const std::int32_t imm =
            static_cast<std::int32_t>((w & 0x03FFFFFF) << 6) >> 4;
        return target(static_cast<long>(i) + imm / 4);
    }
    if (b == 0x54 || (b & 0x7F) == 0x34 || (b & 0x7F) == 0x35 ||
        (b & 0x7E) == 0x36) {
        std::int32_t off = 0;
        if (b == 0x54)
            off = static_cast<std::int32_t>(((w >> 5) & 0x7FFFF) << 13) >> 11;
        else if ((b & 0x7E) == 0x36)
            off = static_cast<std::int32_t>(((w >> 5) & 0x3FFF) << 18) >> 16;
        else
            off = static_cast<std::int32_t>(((w >> 5) & 0x7FFFF) << 13) >> 11;
        Edge e = target(static_cast<long>(i) + off / 4);
        if (e == Edge::Escapes) return Edge::Escapes;
        return target(static_cast<long>(i) + 1);
    }
    if (!decode(w).known) {
        // Every branch encoding (direct/indirect/exception) decodes, so an
        // unrecognized word falls through; x18-shaped may hide a lane.
        const Touch t = touch18(w);
        if (t.use) return Edge::Escapes;
    }
    return target(static_cast<long>(i) + 1);
}

// Forward exit liveness: the value live after word d (absolute index, inside
// [rs,re)) dies on every path iff no unrenamed use can observe it. Words in
// [rs,re) are renamed (uses continue, defs kill); ret/calls kill per NDK
// convention (caller-saved scratch); anything else leaving the window kills
// the rewrite, not the value. Bounded: exhaustion is an escape.
bool trailingDies(const std::uint32_t* win, std::size_t n, std::size_t rs,
                  std::size_t re, std::size_t d, bool complete,
                  const std::vector<std::pair<std::size_t, std::size_t>>*
                      covered) {
    std::vector<char> seen(n, 0);
    std::vector<std::size_t> wl;
    {
        std::vector<std::size_t> first;
        if (succEdge(win, n, d, first, complete, covered) == Edge::Escapes)
            return false;
        wl = std::move(first);
    }
    std::size_t visits = 0;
    while (!wl.empty()) {
        const std::size_t i = wl.back();
        wl.pop_back();
        if (i >= n || seen[i]) continue;
        seen[i] = 1;
        if (++visits > 2048) return false;
        const Touch t = touch18(win[i]);
        if (t.use && (i < rs || i >= re)) return false;  // unrenamed observer
        // Unshaped unknowns fall through (all branch classes decode); shaped
        // ones set t.use above. In-range words are pre-checked, so an
        // in-range use reads the renamed value and stays live.
        if (t.def) continue;  // redefined: this path carries a new value
        std::vector<std::size_t> nx;
        if (succEdge(win, n, i, nx, complete, covered) == Edge::Escapes)
            return false;
        for (std::size_t s : nx) {
            if (!seen[s]) wl.push_back(s);
        }
    }
    return true;
}

// Rewrite one function range in place. Returns sites renamed, or -1 to skip
// (reason stored in why when provided).
long rewriteRange(std::uint32_t* code, std::size_t nwords, X18Stats& st,
                  const char** why = nullptr,
                  const std::uint32_t* full = nullptr, std::size_t absStart = 0,
                  std::size_t fullN = 0, bool complete = true,
                  const std::vector<std::pair<std::size_t, std::size_t>>*
                      covered = nullptr) {
    if (nwords == 0) return 0;
    // Reachability: literal pools sit after unconditional control flow and
    // must never be patched as code. Mark from entry following branches.
    std::vector<char> live(nwords, 0);
    {
        std::vector<std::size_t> stack;
        stack.push_back(0);
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            if (i >= nwords || live[i]) continue;
            live[i] = 1;
            const std::uint32_t w = code[i];
            const unsigned b = w >> 24;
            auto push = [&](std::size_t j) {
                if (j < nwords && !live[j]) stack.push_back(j);
            };
            if (b == 0xD6) {
                // br/ret/eret end flow; blr returns (bits[23:16] == 0x3F).
                if (((w >> 16) & 0xFF) == 0x3F) {
                    push(i + 1);
                }
                continue;
            }
            if ((b & 0xFC) == 0x14 || (b & 0xFC) == 0x94) {
                if ((b & 0xFC) == 0x14) {
                    // b: direct target only.
                    std::int32_t imm =
                        static_cast<std::int32_t>((w & 0x03FFFFFF) << 6) >> 4;
                    const long t = static_cast<long>(i) + imm / 4;
                    if (t >= 0) push(static_cast<std::size_t>(t));
                } else {
                    push(i + 1);  // bl returns
                }
                continue;
            }
            if (b == 0x54 || (b & 0x7F) == 0x34 || (b & 0x7F) == 0x35 ||
                (b & 0x7E) == 0x36) {
                // Conditional: target + fallthrough.
                std::int32_t off = 0;
                if (b == 0x54)
                    off = static_cast<std::int32_t>(((w >> 5) & 0x7FFFF) << 13) >> 11;
                else if ((b & 0x7E) == 0x36)
                    off = static_cast<std::int32_t>(((w >> 5) & 0x3FFF) << 18) >> 16;
                else
                    off = static_cast<std::int32_t>(((w >> 5) & 0x7FFFF) << 13) >> 11;
                const long t = static_cast<long>(i) + off / 4;
                if (t >= 0) push(static_cast<std::size_t>(t));
                push(i + 1);
                continue;
            }
            push(i + 1);  // straight-line (unknowns have no control flow)
        }
    }
    // Pass 1: decode everything, collecting refs (pos + define/use),
    // calls, mem traffic, and unknowns.
    struct Ref {
        std::size_t idx;
        unsigned pos;
        bool isDef;
    };
    std::vector<Ref> refs;
    std::vector<std::size_t> calls;
    std::uint32_t used = 0;
    std::vector<std::uint32_t> words(code, code + nwords);
    static int traceBlock = -1;
    if (traceBlock < 0) {
        const char* v = std::getenv("KUDROID_X18_TRACE");
        traceBlock = (v != nullptr && v[0] == '1') ? 1 : 0;
    }
    for (std::size_t i = 0; i < nwords; ++i) {
        // Unreachable words are data (literal pools), not code: a word that
        // really touches x18 aborts the rename (it may be code entered from
        // outside); anything else cannot affect x18 flow and is ignored.
        // Decode-aware: crude slot scans mistake offset bits for registers.
        if (!live[i]) {
            const Touch dt = touch18(words[i]);
            if (dt.use || dt.def) {
                ++st.skippedRange;
                if (why) *why = "data";
                if (traceBlock)
                    std::fprintf(stderr, "[KuDroidELF] x18 block +%zx\n", i * 4);
                return -1;
            }
            continue;
        }
        const Decoded d = decode(words[i]);
        used |= d.used;
        if (d.mem18) {
            // Value crosses memory: no in-chunk rename stays consistent.
            ++st.skippedNoReg;
            if (why) *why = "mem";
            return -1;
        }
        if (!d.known && (((words[i] >> 0) & 31) == kX18 ||
                         ((words[i] >> 5) & 31) == kX18 ||
                         ((words[i] >> 16) & 31) == kX18 ||
                         ((words[i] >> 10) & 31) == kX18)) {
            ++st.skippedUnknown;
            if (why) *why = "unknown";
            return -1;
        }
        for (unsigned k = 0; k < d.npatch; ++k) {
            refs.push_back({i, d.at[k], (d.defmask & (1u << k)) != 0});
        }
        for (unsigned k = 0; k < d.nuse; ++k) {
            const unsigned pos = d.useonly[k];
            if (((words[i] >> pos) & 31) == kX18) {
                // Opaque slot holding x18: cannot rename, cannot ignore.
                ++st.skippedUnknown;
                if (why) *why = "opaque";
                return -1;
            }
        }
        if (hasCall(words[i])) calls.push_back(i);
    }
    if ((used & (1u << kX18)) == 0) return 0;  // x18-free: nothing to do
    // Span check: every use must be fed by a define in-range (no live-in),
    // no call may sit inside a span (a caller-saved substitute would not
    // survive it), and the value live after the last x18 reference must die
    // on every path (no unrenamed observer). Within one instruction, uses
    // precede defines (add x18,x18 reads first).
    {
        long lastDef = -1;
        long lastX18 = -1;
        struct Span {
            long a, b;
        };
        std::vector<Span> spans;
        std::size_t k = 0;
        auto handle = [&](const Ref& r) {
            const std::uint32_t w = words[r.idx];
            if (((w >> r.pos) & 31) != kX18) return true;
            lastX18 = static_cast<long>(r.idx);
            if (r.isDef) {
                lastDef = static_cast<long>(r.idx);
            } else {
                if (lastDef < 0) {
                    ++st.skippedNoReg;
                    if (why) *why = "livein";
                    return false;
                }
                spans.push_back({lastDef, static_cast<long>(r.idx)});
            }
            return true;
        };
        bool okSpan = true;
        while (k < refs.size()) {
            const std::size_t idx = refs[k].idx;
            std::size_t j = k;
            while (j < refs.size() && refs[j].idx == idx) ++j;
            for (std::size_t m = k; m < j && okSpan; ++m) {
                if (!refs[m].isDef && !handle(refs[m])) okSpan = false;
            }
            for (std::size_t m = k; m < j && okSpan; ++m) {
                if (refs[m].isDef && !handle(refs[m])) okSpan = false;
            }
            if (!okSpan) return -1;
            k = j;
        }
        if (lastX18 >= 0) {
            const std::uint32_t* win = (full != nullptr) ? full : code;
            const std::size_t wn = (full != nullptr) ? fullN : nwords;
            const std::size_t rs = (full != nullptr) ? absStart : 0;
            if (!trailingDies(win, wn, rs, rs + nwords,
                              rs + static_cast<std::size_t>(lastX18), complete,
                              covered)) {
                ++st.skippedNoReg;
                if (why) *why = "liveout";
                return -1;
            }
        }
        for (const auto& s : spans) {
            for (std::size_t c : calls) {
                if (static_cast<long>(c) > s.a && static_cast<long>(c) < s.b) {
                    ++st.skippedNoReg;
                    if (why) *why = "callspan";
                    return -1;
                }
            }
        }
    }
    const unsigned sub = pickFreeReg(used);
    if (sub >= 31) {
        ++st.skippedNoReg;
        if (why) *why = "noreg";
        return -1;
    }
    // Pass 2: patch every recorded field holding 18 (reachable code only).
    long sites = 0;
    for (std::size_t i = 0; i < nwords; ++i) {
        if (!live[i]) continue;
        const Decoded d = decode(words[i]);
        std::uint32_t w = words[i];
        for (unsigned k = 0; k < d.npatch; ++k) {
            const unsigned pos = d.at[k];
            if (((w >> pos) & 31) == kX18) {
                w = (w & ~(31u << pos)) | (sub << pos);
                ++sites;
            }
        }
        code[i] = w;
    }
    return sites;
}

}  // namespace
}  // namespace x18

bool elf_x18_enabled() {
    static int enabled = -1;
    if (enabled < 0) {
        const char* v = std::getenv("KUDROID_X18_REWRITE");
        enabled = (v != nullptr && v[0] == '0') ? 0 : 1;
        if (!enabled) {
            std::fprintf(stderr, "[KuDroidELF] x18 rewrite disabled by env\n");
        }
    }
    return enabled != 0;
}

X18Stats elf_x18_rewrite(const void* base, std::uint64_t minVaddr,
                         const std::vector<ElfLoader::Segment>& segments,
                         const std::uint8_t* fileData, std::size_t fileSize) {
    X18Stats st;
    if (!elf_x18_enabled() || base == nullptr || fileData == nullptr || fileSize < 64) {
        return st;
    }
    // Locate .eh_frame through section headers (file view).
    std::uint64_t shoff = 0;
    std::uint16_t shentsize = 0, shnum = 0, shstrndx = 0;
    std::memcpy(&shoff, fileData + 0x28, 8);
    std::memcpy(&shentsize, fileData + 0x3A, 2);
    std::memcpy(&shnum, fileData + 0x3C, 2);
    std::memcpy(&shstrndx, fileData + 0x3E, 2);
    const std::uint8_t* ehMap = nullptr;
    std::size_t ehLen = 0;
    if (shoff != 0 && shentsize >= 64 && shnum > 0 && shstrndx < shnum &&
        shoff + static_cast<std::uint64_t>(shnum) * shentsize <= fileSize) {
        const std::uint8_t* s = fileData + shoff + shstrndx * shentsize;
        std::uint64_t so = 0, ss = 0;
        std::memcpy(&so, s + 24, 8);
        std::memcpy(&ss, s + 32, 8);
        if (so <= fileSize && ss <= fileSize - so && ss > 0) {
            const char* shstr = reinterpret_cast<const char*>(fileData + so);
            for (std::uint16_t i = 0; i < shnum; ++i) {
                const std::uint8_t* e = fileData + shoff + i * shentsize;
                std::uint32_t name = 0;
                std::uint64_t addr = 0, size = 0;
                std::memcpy(&name, e + 0, 4);
                std::memcpy(&addr, e + 16, 8);
                std::memcpy(&size, e + 32, 8);
                if (name >= ss) continue;
                if (std::strcmp(shstr + name, ".eh_frame") == 0 && size > 16 &&
                    addr >= minVaddr) {
                    ehMap = static_cast<const std::uint8_t*>(base) + (addr - minVaddr);
                    ehLen = static_cast<std::size_t>(size);
                    break;
                }
            }
        }
    }
    if (ehMap == nullptr || ehLen == 0) return st;
    std::vector<x18::FdeRange> ranges;
    x18::walkEhFrame(ehMap, ehLen, ranges);
    const std::uint8_t* img = static_cast<const std::uint8_t*>(base);
    const std::uint64_t imgAddr = reinterpret_cast<std::uint64_t>(img);
    (void)minVaddr;  // addressing is done in runtime coordinates throughout
    for (const auto& r : ranges) {
        if (r.end <= r.start || r.start < imgAddr) {
            ++st.skippedRange;
            continue;
        }
        if (r.personality) {
            ++st.skippedEh;
            continue;
        }
        // Clip to an EXEC segment (runtime coordinates throughout).
        const std::uint8_t* segBase = nullptr;
        std::size_t segLen = 0;
        for (const auto& seg : segments) {
            if ((seg.flags & 1) == 0) continue;
            const std::uint64_t s0 = imgAddr + (seg.vaddr - minVaddr);
            const std::uint64_t s1 = s0 + seg.filesz;
            if (r.start >= s0 && r.end <= s1) {
                segBase = img + (seg.vaddr - minVaddr);
                segLen = static_cast<std::size_t>(seg.filesz);
                break;
            }
        }
        if (segBase == nullptr) {
            ++st.skippedRange;
            continue;
        }
        const std::size_t segOff =
            static_cast<std::size_t>(r.start - (imgAddr + (segBase - img)));
        const std::size_t len = static_cast<std::size_t>(r.end - r.start);
        if (segOff % 4 != 0 || len % 4 != 0 || segOff + len > segLen) {
            ++st.skippedRange;
            continue;
        }
        ++st.functions;
        const long sites = x18::rewriteRange(
            reinterpret_cast<std::uint32_t*>(const_cast<std::uint8_t*>(segBase) + segOff),
            len / 4, st);
        if (sites > 0) {
            ++st.rewritten;
            st.sites += static_cast<std::uint64_t>(sites);
        }
        static int traceOn = -1;
        if (traceOn < 0) {
            const char* v = std::getenv("KUDROID_X18_TRACE");
            traceOn = (v != nullptr && v[0] == '1') ? 1 : 0;
        }
        if (traceOn) {
            // File offset for offline inspection with objdump.
            const std::size_t fileOff =
                static_cast<std::size_t>(segBase - img) + segOff;
            if (sites > 0) {
                std::fprintf(stderr, "[KuDroidELF] x18 range %llx sites=%ld @%zx\n",
                             (unsigned long long)r.start, sites, fileOff);
                continue;
            }
            X18Stats dummy;
            const char* why = "";
            x18::rewriteRange(
                reinterpret_cast<std::uint32_t*>(const_cast<std::uint8_t*>(segBase) +
                                                 segOff),
                len / 4, dummy, &why);
            std::fprintf(stderr, "[KuDroidELF] x18 range %llx skip=%s @%zx\n",
                         (unsigned long long)r.start, why, fileOff);
        }
    }
    // Gap pass: FDE-less code (hand-tuned DSP without unwind tables) gets
    // entries from prologue shapes, ret-fallthrough, and computed targets.
    // Each chunk is likeness-gated (pools fail it) and span-validated.
    for (const auto& seg : segments) {
        if ((seg.flags & 1) == 0) continue;
        const std::uint64_t s0 = imgAddr + (seg.vaddr - minVaddr);
        const std::size_t nwords = static_cast<std::size_t>(seg.filesz) / 4;
        std::uint32_t* words = reinterpret_cast<std::uint32_t*>(
            const_cast<std::uint8_t*>(img) + (seg.vaddr - minVaddr));
        // Covered intervals from the FDE pass above, converted to word
        // indices (ranges are byte addresses).
        std::vector<std::pair<std::size_t, std::size_t>> covered;
        for (const auto& r : ranges) {
            if (r.end <= r.start) continue;
            if (r.start >= s0 && r.end <= s0 + seg.filesz) {
                covered.emplace_back(static_cast<std::size_t>(r.start - s0) / 4,
                                     static_cast<std::size_t>(r.end - s0 + 3) / 4);
            }
        }
        std::sort(covered.begin(), covered.end());
        {
            std::size_t mx = 0;
            for (const auto& c : covered) mx = std::max(mx, c.second);
            std::fprintf(stderr, "[KuDroidELF] covered=%zu maxend=%zu nwords=%zu\n",
                         covered.size(), mx, nwords);
        }
        // Walk the segment; each maximal uncovered run is gap-processed.
        std::size_t cur = 0;
        auto flushGap = [&](std::size_t gs, std::size_t ge) {
            if (ge <= gs + 4) return;  // need room for code, not noise
            if (ge > nwords || gs >= nwords) {
                std::fprintf(stderr,
                             "[KuDroidELF] gap OOB gs=%zu ge=%zu nwords=%zu "
                             "segv=%llx segfilesz=%zx ranges=%zu\n",
                             gs, ge, nwords, (unsigned long long)seg.vaddr, seg.filesz,
                             ranges.size());
                return;
            }
            // Prologue-delimited ranges: one substitute per function-like
            // range. x18 flows through loops, so splitting by branch would
            // cut live values; the span + exit-liveness rules validate.
            // Also split after terminal control flow (ret/uncond-b/br):
            // what follows is data, a cold block, or a function entered
            // from outside (tail-called code has no prologue spill).
            // Inter-range flow stays sound: the exit-liveness walk follows
            // it and escapes on any unrenamed observer.
            std::vector<std::size_t> splits;
            splits.push_back(gs);
            for (std::size_t i = gs + 1; i < ge; ++i) {
                const std::uint32_t w = words[i];
                if (x18::isEntryWord(w)) {
                    const unsigned b = w >> 24;
                    if (b == 0x90 || b == 0xB0 || b == 0xD0 || b == 0xF0 ||
                        (b & 0xFC) == 0x14)
                        continue;  // adrp/b-thunks are flow, not entries
                    splits.push_back(i);
                    continue;
                }
                const std::uint32_t pw = words[i - 1];
                const unsigned pb = pw >> 24;
                const bool term =
                    (pb == 0xD6 && ((pw >> 16) & 0xFF) != 0x3F) ||  // ret/br
                    ((pb & 0xFC) == 0x14);                          // b
                if (term && i < ge) splits.push_back(i);
            }
            splits.push_back(ge);
            static int traceGap = -1;
            if (traceGap < 0) {
                const char* v = std::getenv("KUDROID_X18_TRACE");
                traceGap = (v != nullptr && v[0] == '1') ? 1 : 0;
            }
            for (std::size_t s = 0; s + 1 < splits.size(); ++s) {
                const std::size_t cs = splits[s];
                const std::size_t ce = splits[s + 1];
                if (ce <= cs + 1) continue;
                if (!x18::looksLikeCode(words + cs, ce - cs)) {
                    ++st.skippedRange;
                    continue;
                }
                ++st.functions;
                const char* why0 = "";
                const long sites = x18::rewriteRange(
                    words + cs, ce - cs, st, &why0, words + gs, cs - gs,
                    ge - gs, false, &covered);
                if (sites > 0) {
                    ++st.rewritten;
                    st.sites += static_cast<std::uint64_t>(sites);
                }
                if (traceGap) {
                    const std::size_t fileOff = seg.offset + (cs * 4);
                    if (sites > 0) {
                        std::fprintf(stderr, "[KuDroidELF] x18 gap %zx sites=%ld\n",
                                     fileOff, sites);
                    } else {
                        std::fprintf(stderr, "[KuDroidELF] x18 gap %zx skip=%s\n",
                                     fileOff, why0);
                    }
                }
            }
        };
        for (const auto& c : covered) {
            if (c.first > cur) flushGap(cur, c.first);
            if (c.second > cur) cur = c.second;
        }
        if (cur < nwords) flushGap(cur, nwords);
    }
    return st;
}

}  // namespace kudroid
