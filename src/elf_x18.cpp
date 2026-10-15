// AOT x18 rewrite (see ElfX18.h for the soundness rules).
#include "kudroid/ElfX18.h"
#include "kudroid/ExecMemory.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

namespace kudroid {
namespace {
std::mutex& s_tlsStubMutex() {
    static std::mutex mtx;
    return mtx;
}
}  // namespace
}  // namespace kudroid

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
    std::uint8_t defonly[4] = {0, 0, 0, 0};
    std::uint8_t ndef = 0;
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

    // Branches with register: br/blr/ret read Rn@9:5 only (bits[20:16] are
    // fixed opcode bits, not a register — left alone).
    if (b == 0xD6) {
        markPatch(d, 5, rn, false);
        return d;
    }
    // Authenticated branches: BRAA/BRAAZ/BLRAA/BLRAAZ/RETAA/RETAB/ERETAA.
    // Layout (capstone-verified: 0xD71F0812 = braa x0, x18): target Rn@9:5,
    // modifier Rm@4:0 — NOT @20:16 (those bits are fixed opcode 11111, and
    // there is no third register despite the audit's Rm@16 claim). Both
    // patch sites fire only when the field actually reads x18. Z/ERET forms
    // read nothing. Without this case they fell to unknown and skipped the
    // whole function (safe but lost rewrites); worse, a future reader of
    // this table could misplace the modifier and leave a live x18 that async
    // delivery zeroes.
    if (b == 0xD7) {
        markPatch(d, 5, rn, false);
        markPatch(d, 0, rd, false);
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
    // Data-processing immediate group (bits[28:25] == 1001: logical, move wide,
    // bitfield, extract) needs the op field. Bit 28 matters: without it any
    // word with bits[27:25] == 001, such as a float constant in a literal pool,
    // decodes as an instruction and gets rewritten.
    if (((w >> 25) & 0xF) == 9) {
        const unsigned op = (w >> 23) & 3;
        if (op == 1) {
            if (((w >> 29) & 3) == 3) markPatch(d, 0, rd, false);
            markPatch(d, 0, rd, true);  // mov wide
        } else if (op == 3) {
            markPatch(d, 0, rd, true);  // extr
            markPatch(d, 5, rn, false);
            markPatch(d, 16, rm, false);
        } else {
            if (op == 2 && ((w >> 29) & 3) == 1) markPatch(d, 0, rd, false);
            markPatch(d, 0, rd, true);  // logical-imm / bitfield
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
    // ADCS/SBCS (bit 22 clear) and CCMN/CCMP (bit 22 set). The immediate form of
    // CCMN/CCMP keeps imm5 in bits[20:16], which is not a register.
    if ((b & 0x3F) == 0x3A) {
        if (((w >> 22) & 1) != 0) {
            markPatch(d, 5, rn, false);
            if ((w & (1u << 11)) == 0) markPatch(d, 16, rm, false);
        } else {
            markPatch(d, 0, rd, true);
            markPatch(d, 5, rn, false);
            markPatch(d, 16, rm, false);
        }
        return d;
    }
    // Conditional select / multiply: Ra field is real, except fixed patterns
    // (sdiv Ra=11111) which never equal 18 and are safe to leave marked.
    if (b == 0x1A || b == 0x5A || b == 0x9A || b == 0xDA || b == 0x1B || b == 0x9B) {
        markPatch(d, 0, rd, true);
        markPatch(d, 5, rn, false);
        markPatch(d, 16, rm, false);
        // Multiply-add (madd/msub/smaddl/umaddl...) names a real accumulator register
        // in bits[14:10]; the select/adc group keeps condition and opcode bits there.
        if (b == 0x1B || b == 0x9B) markPatch(d, 10, ra, false);
        else markUseOnly(d, 10, ra);
        return d;
    }
    // Loads/stores, single: size 111 V 00 (unscaled/pre/post/reg-offset/LSE)
    // and size 111 V 01 (unsigned offset). Bit 25 set is not a load/store
    // (ccmp, adcs, ...), which is why the mask keeps bit 1 clear.
    if ((b & 0x3A) == 0x38) {
        const bool simd = (w & (1u << 26)) != 0;
        const bool unsignedOff = (b & 1) != 0;
        const bool bit21 = (w & (1u << 21)) != 0;
        const unsigned op2 = (w >> 10) & 3;
        const unsigned opc = (w >> 22) & 3;
        // PRFM / PRFUM: Rt is a prefetch operation, not a register.
        const bool prefetch = !simd && (b & 0xC0) == 0xC0 && opc == 2 &&
                              (unsignedOff || (!bit21 && op2 == 0) || (bit21 && op2 == 2));
        if (prefetch) {
            markPatch(d, 5, rn, false);
            if (bit21 && op2 == 2) markPatch(d, 16, rm, false);
            return d;
        }
        // LSE atomics (integer only, bit 21 set, bits[11:10] == 00) carry an Rs
        // source the plain forms lack. Plain unscaled forms keep an offset in
        // bits[20:12]; the unsigned-offset form keeps one in bits[21:10].
        const bool lse = !unsignedOff && !simd && bit21 && op2 == 0;
        // Register offset: bit 21 set with bits[11:10] == 10.
        const bool regOffset = !unsignedOff && bit21 && op2 == 2;
        // Nonzero opc selects a load, including sign-extending forms.
        const bool isLoad = opc != 0;
        if (lse) markPatch(d, 16, rm, false);
        if (!simd) markPatch(d, 0, rd, isLoad);
        markPatch(d, 5, rn, false);
        if (regOffset) markPatch(d, 16, rm, false);
        return d;
    }
    // Load register (literal): PC-relative literal load into Rt (integer forms).
    if ((b & 0x3F) == 0x18) {
        const unsigned opc = (w >> 30) & 3;
        if (opc != 3) {
            markPatch(d, 0, rd, true);
        }
        return d;
    }
    if ((b & 0x3F) == 0x1C) {
        // SIMD literal load: target is vector register (Vt), Darwin preserves v18.
        return d;
    }
    // Loads/stores, pair. Integer lanes share top bytes with SIMD lanes;
    // only Rn is an integer register in the SIMD form. Store forms (opc
    // bit22 clear) read their lanes; load forms define them.
    if ((b & 0x3E) == 0x28) {
        const bool isPairLoad = ((w >> 22) & 1) != 0;
        markPatch(d, 0, rd, isPairLoad);
        markPatch(d, 10, ra, isPairLoad);
        markPatch(d, 5, rn, false);
        return d;
    }
    // SIMD pairs (ldnp/stnp/ldp/stp): top bytes 0x2C/0x2D and the 0x6C/0xAC
    // size variants. 0x2E/0x2F share the high bits but are vector arithmetic.
    if ((b & 0x3E) == 0x2C) {
        markPatch(d, 5, rn, false);
        return d;
    }
    // Advanced SIMD load/store structure forms. One integer register only: the
    // base. Rt@4:0 is a VECTOR register, and Darwin does preserve v18 across
    // context switches, so it is left alone by the same rule as the plain SIMD
    // ldr/st arm above.
    //
    // Four top bytes carry the whole family, each confirmed against the
    // assembler: 0x0C ld1 {v0.4h} / ld1 {v0.8b}, 0x0D ld1 {v18.s}[1] and the
    // single-element forms, 0x4C ld1/st1/ld2/st2/ld3/st3/ld4/st4, 0x4D the
    // replicate forms ld1r/ld2r/ld3r/ld4r. The neighbouring top bytes are not
    // part of it and must not be absorbed: 0x2C/0x6C/0xAC/0xEC are the
    // non-temporal pairs (ldnp/ldtnp, matched by the arm above) and
    // 0x8C/0x8D/0xCC/0xCD plus 0x0E/0x0F/0x2E/0x4E/0x6E disassemble as
    // undefined. A mask loose enough to include them (0xBE over b, say) would
    // mark an undecodable word as known, and a "known" word is one this pass is
    // then allowed to patch.
    //
    // Before this arm every one of these instructions fell through to unknown,
    // and a function containing one was skipped whole with its x18 base left in
    // place: observed live as libunity+0xcf6140, ld1 {v18.s}[1], [x18]
    // (0x0d409252), faulting at address 0 with x18 zeroed by a Darwin delivery.
    if (b == 0x0C || b == 0x0D || b == 0x4C || b == 0x4D) {
        markPatch(d, 5, rn, false);
        // Bit 23 -- not the top byte -- is the post-index bit, so both forms
        // share one of the four top bytes above (ld1 {v0.4h}, [x0] and
        // ld1 {v0.4h}, [x18], x18 are both 0x0C). In the register form Rm@20:16
        // is a second integer register; in the immediate form it is pinned to
        // 31, which is what separates the two (the assembler rejects x31 as a
        // writeback register and allows only the structure size as an
        // immediate, so 31 never names a real register here).
        if ((w & (1u << 23)) != 0 && rm != 31) {
            markPatch(d, 16, rm, false);
        }
        return d;
    }
    // Advanced SIMD / scalar-FP data processing. Vector forms (v18) touch no
    // integer register except the SIMD<->GPR moves; scalar FP crosses between
    // FP lanes and GPRs only in the conversion classes.
    const unsigned bLow5 = b & 0x1F;
    if (bLow5 == 0x0E) {
        // Advanced SIMD copy with a general register: DUP/INS (Rn reads) and
        // SMOV/UMOV (Rd defines). Bit 29 (op) must be clear; set it is INS
        // element, which is vector-to-vector. Darwin preserves v18, so register
        // 18 in any vector lane needs nothing.
        d.known = true;
        if ((w & 0xBFE08400u) == 0x0E000400u) {
            const unsigned imm4 = (w >> 11) & 0xF;
            if (imm4 == 1 || imm4 == 3) markPatch(d, 5, rn, false);
            else if (imm4 == 5 || imm4 == 7) markPatch(d, 0, rd, true);
        }
        return d;
    }
    // Advanced SIMD by-element, shift-by-immediate and modified-immediate forms
    // (top bytes 0x0F/0x2F/0x4F/0x6F) and their scalar counterparts (0x5F/0x7F): vector
    // registers and immediates only. A field equal to 18 here is v18 or an immediate
    // bit, and treating it as unrecognised made every function containing one skip the
    // rewrite and keep its real x18 uses.
    if (((b & 0x9F) == 0x0F) || b == 0x5F || b == 0x7F) {
        d.known = true;
        return d;
    }
    // Advanced SIMD scalar (top bytes 0x5E/0x7E): FP lanes only.
    if (b == 0x5E || b == 0x7E) {
        d.known = true;
        return d;
    }
    if ((b & 0x7F) == 0x1E) {
        // Scalar FP. Only the FP<->integer conversion classes name a GPR; every
        // other class (fadd, fcmp, fcsel, fmov imm, fcvt fp-fp) has bit 21 set
        // and a nonzero bits[15:10], and its Rm is an FP register that must not
        // be read as a GPR.
        //   bit 21 set, bits[15:10] == 0: convert/fmov, opcode = bits[18:16]
        //   bit 21 clear: fixed-point convert, opcode = bits[18:16]
        // Opcodes 0/1/4/5/6 write a GPR (fcvt*, fmov to GPR); 2/3/7 read one
        // (scvtf/ucvtf, fmov from GPR).
        d.known = true;
        const bool bit21 = (w & (1u << 21)) != 0;
        if (bit21 && ((w >> 10) & 0x3F) != 0) return d;
        const unsigned opcode = (w >> 16) & 7;
        if (bit21) {
            if (opcode <= 1 || opcode == 4 || opcode == 5 || opcode == 6) {
                markPatch(d, 0, rd, true);
            } else {
                markPatch(d, 5, rn, false);
            }
            return d;
        }
        const unsigned rmode = (w >> 19) & 3;
        if (rmode == 3 && opcode <= 1) {
            markPatch(d, 0, rd, true);  // fcvtzs/fcvtzu (fixed-point)
        } else if (rmode == 0 && (opcode == 2 || opcode == 3)) {
            markPatch(d, 5, rn, false);  // scvtf/ucvtf (fixed-point)
        }
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
    // Remaining space (SVE, unallocated, exclusive LDXR/STXR which hide a
    // value lane the bit shapes above do not prove): unknown — slot scan,
    // skip-if-18, never patch.
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
    // Store (64-bit) with Rn=SP: the mask clears imm9, the index bits and Rt, so this
    // covers the unscaled/pre/post-index forms. The constant used to be 0xF81003E0, which
    // has bit 20 set — inside the masked-out range — so the test was never true (gcc
    // reports the tautology) and this prologue shape was never recognised.
    if ((w & 0xFFC003E0) == 0xF80003E0) return true;  // str *,[sp,#-x]
    if (w == 0x910003FD) return true;                 // mov x29,sp
    if (b == 0x90 || b == 0xB0 || b == 0xD0 || b == 0xF0) return true;  // adrp
    if ((b & 0xFC) == 0x14) return true;              // b (thunk)
    if (w == 0xD503237F || w == 0xD503245F) return true;  // pacibsp / bti c
    return false;
}

// Fraction of known-class words over the chunk (first 256): real code scores
// above 95%, pools and embedded data blobs (addresses, floats, high-entropy
// words) score about half.
bool looksLikeCode(const std::uint32_t* code, std::size_t nwords) {
    std::size_t n = nwords < 256 ? nwords : 256;
    if (n == 0) return false;
    std::size_t known = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (decode(code[i]).known) ++known;
    }
    return known * 100 >= n * 85;
}

// Registers a word reads or writes (integer lanes only). Unknown words
// report every register-shaped slot as a read — conservative for callers
// that must not disturb a live value.
struct RegTouch {
    std::uint32_t defs = 0;
    std::uint32_t uses = 0;
};

RegTouch regTouch(std::uint32_t w) {
    RegTouch t;
    const Decoded d = decode(w);
    if (!d.known) {
        t.uses = (1u << ((w >> 0) & 31)) | (1u << ((w >> 5) & 31)) |
                 (1u << ((w >> 16) & 31)) | (1u << ((w >> 10) & 31));
        t.uses &= 0x7FFFFFFFu;  // 31 is not a register
        return t;
    }
    for (unsigned k = 0; k < d.npatch; ++k) {
        const unsigned r = (w >> d.at[k]) & 31;
        if (r >= 31) continue;
        ((d.defmask >> k) & 1 ? t.defs : t.uses) |= 1u << r;
    }
    for (unsigned k = 0; k < d.nuse; ++k) {
        const unsigned r = (w >> d.useonly[k]) & 31;
        if (r < 31) t.uses |= 1u << r;
    }
    for (unsigned k = 0; k < d.ndef; ++k) {
        const unsigned r = (w >> d.defonly[k]) & 31;
        if (r < 31) t.defs |= 1u << r;
    }
    return t;
}

// A register is free for span [a, b] iff no instruction inside the window
// touches it AND it has no live range crossing the window: defined before
// the span and read after it (or read before and defined after — symmetric
// hazard through the same physical register is impossible for a value both
// written and read around an untouched window, but the def-before/read-after
// shape is the one a rename would corrupt, and read-before/def-after is
// harmless since the span's write would be overwritten first... it is NOT
// harmless for the span's own value: the later def overwrites the substitute
// mid-span? No — the def is outside the window; the span's uses all read the
// substitute before it. Only def-before + use-after destroys a crossing
// value). Calls inside the span are rejected before allocation, so clobber
// survival is not a concern here.
bool hasCall(std::uint32_t w) {
    const unsigned b = w >> 24;
    return (b & 0xFC) == 0x94 ||
           (w & 0xFFFFFC1F) == 0xD63F0000 ||
           (w & 0xFFFFFC00) == 0xD73F0800 ||
           (w & 0xFFFFFC00) == 0xD73F0C00 ||
           (w & 0xFFFFFC1F) == 0xD63F081F ||
           (w & 0xFFFFFC1F) == 0xD63F0C1F;
}

// Caller-saved integer candidates (x16/x17 excluded: linker veneers;
// x29/SP excluded structurally, x30 as last-resort fallback).
unsigned pickFreeReg(std::uint32_t used) {
    static const unsigned kOrder[] = {15, 14, 13, 12, 11, 10, 9, 8,
                                      7,  6,  5,  4,  3,  2,  1,  0, 30};
    for (unsigned r : kOrder) {
        if ((used & (1u << r)) == 0) return r;
    }
    return 31;
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
        if ((w & 0xFFFFFC1Fu) == 0xD65F0000u) return Edge::Dies;  // ret
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

// True when control can leave the window at word i other than through a
// return or a call: a direct branch to an address outside it, an indirect
// jump, or a fall-through off the end. Register values at such an exit are
// consumed by code this pass cannot see.
bool exitsWindow(const std::uint32_t* code, std::size_t n, std::size_t i) {
    const std::uint32_t w = code[i];
    const unsigned b = w >> 24;
    auto outside = [&](long t) { return t < 0 || static_cast<std::size_t>(t) >= n; };
    if (b == 0xD6) {
        if ((w & 0xFFFFFC1Fu) == 0xD65F0000u) return false;  // ret
        if (((w >> 16) & 0xFF) == 0x3F) return false;        // blr: a call
        return ((w >> 5) & 31) != 30;                        // br x30 returns
    }
    if ((b & 0xFC) == 0x94) return false;  // bl
    if ((b & 0xFC) == 0x14) {
        const std::int32_t imm = static_cast<std::int32_t>((w & 0x03FFFFFF) << 6) >> 4;
        return outside(static_cast<long>(i) + imm / 4);
    }
    if (b == 0x54 || (b & 0x7F) == 0x34 || (b & 0x7F) == 0x35 || (b & 0x7E) == 0x36) {
        std::int32_t off = 0;
        if ((b & 0x7E) == 0x36)
            off = static_cast<std::int32_t>(((w >> 5) & 0x3FFF) << 18) >> 16;
        else
            off = static_cast<std::int32_t>(((w >> 5) & 0x7FFFF) << 13) >> 11;
        return outside(static_cast<long>(i) + off / 4) || i + 1 >= n;
    }
    return i + 1 >= n;
}

// Rewrite one function range in place. Returns sites renamed, or -1 to skip
// (reason stored in why when provided).
long rewriteRange(std::uint32_t* code, std::size_t nwords, X18Stats& st,
                  const char** why = nullptr,
                  const std::uint32_t* full = nullptr, std::size_t absStart = 0,
                  std::size_t fullN = 0, bool complete = true,
                  const std::vector<std::pair<std::size_t, std::size_t>>*
                      covered = nullptr,
                  std::size_t* dataAt = nullptr) {
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
                    if (t >= 0) {
                        push(static_cast<std::size_t>(t));
                    }
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
                if (t >= 0) {
                    push(static_cast<std::size_t>(t));
                }
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
                if (dataAt) *dataAt = i;
                if (traceBlock)
                    std::fprintf(stderr, "[KuDroidELF] x18 block +%zx\n", i * 4);
                return -1;
            }
            continue;
        }
        const Decoded d = decode(words[i]);
        used |= d.used;
        if (!d.known && maybeX18(words[i])) {
            ++st.skippedUnknown;
            if (why) *why = "unknown";
            if (traceBlock)
                std::fprintf(stderr, "[KuDroidELF] x18 unknown word %08x +%zx\n", words[i],
                             i * 4);
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
    // Allocate one register for each value connected by control-flow edges.
    std::vector<std::vector<std::size_t>> next(nwords), previous(nwords);
    std::vector<RegTouch> touches(nwords);
    std::vector<Touch> virtualTouch(nwords);
    long lastX18 = -1;
    for (std::size_t i = 0; i < nwords; ++i) {
        if (!live[i]) continue;
        touches[i] = regTouch(words[i]);
        virtualTouch[i] = touch18(words[i]);
        if (virtualTouch[i].use || virtualTouch[i].def) lastX18 = static_cast<long>(i);
        if (hasCall(words[i])) {
            touches[i].uses |= 0xFFu;
            touches[i].defs |= 0x4007FFFFu;
            if (i + 1 < nwords && live[i + 1]) next[i].push_back(i + 1);
        } else {
            if ((words[i] & 0xFFFFFC1Fu) == 0xD65F0000u) touches[i].uses |= 3u;
            // Whatever the code past a window exit reads is live here: the
            // argument registers for a tail call out of a whole function,
            // anything at all when the window is only a slice of one.
            if (exitsWindow(words.data(), nwords, i))
                touches[i].uses |= complete ? 0xFFu : 0x7FFFFFFFu;
            succEdge(words.data(), nwords, i, next[i], complete, nullptr);
        }
        for (std::size_t j : next[i]) previous[j].push_back(i);
    }
    if (lastX18 >= 0) {
        const std::uint32_t* win = full != nullptr ? full : code;
        const std::size_t wn = full != nullptr ? fullN : nwords;
        const std::size_t rs = full != nullptr ? absStart : 0;
        if (!trailingDies(win, wn, rs, rs + nwords,
                          rs + static_cast<std::size_t>(lastX18), complete, covered)) {
            ++st.skippedNoReg;
            if (why) *why = "liveout";
            return -1;
        }
    }

    std::vector<std::uint32_t> in(nwords), out(nwords);
    std::vector<char> vxIn(nwords), vxOut(nwords), queued(nwords);
    std::vector<std::size_t> work;
    for (std::size_t i = 0; i < nwords; ++i) {
        if (live[i]) { work.push_back(i); queued[i] = 1; }
    }
    while (!work.empty()) {
        const std::size_t i = work.back();
        work.pop_back();
        queued[i] = 0;
        std::uint32_t newOut = 0;
        bool newVxOut = false;
        for (std::size_t j : next[i]) {
            newOut |= in[j];
            newVxOut |= vxIn[j] != 0;
        }
        const std::uint32_t newIn = touches[i].uses | (newOut & ~touches[i].defs);
        const bool newVxIn = virtualTouch[i].use || (newVxOut && !virtualTouch[i].def);
        out[i] = newOut;
        vxOut[i] = newVxOut;
        if (newIn == in[i] && newVxIn == (vxIn[i] != 0)) continue;
        in[i] = newIn;
        vxIn[i] = newVxIn;
        for (std::size_t j : previous[i]) {
            if (!queued[j]) { work.push_back(j); queued[j] = 1; }
        }
    }
    if (vxIn[0]) {
        ++st.skippedNoReg;
        if (why) *why = "livein";
        return -1;
    }
    for (std::size_t i : calls) {
        if (vxIn[i] && vxOut[i]) {
            ++st.skippedNoReg;
            if (why) *why = "callspan";
            return -1;
        }
    }

    // Before/after states merge at joins and across read-modify-write operands.
    std::vector<std::size_t> parent(nwords * 2);
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = i;
    auto root = [&](std::size_t i) {
        while (parent[i] != i) {
            parent[i] = parent[parent[i]];
            i = parent[i];
        }
        return i;
    };
    auto join = [&](std::size_t a, std::size_t b) {
        parent[root(a)] = root(b);
    };
    for (std::size_t i = 0; i < nwords; ++i) {
        if (!live[i]) continue;
        const Touch& t = virtualTouch[i];
        if ((vxIn[i] && vxOut[i] && !t.def) || (t.use && t.def)) join(2 * i, 2 * i + 1);
        if (vxOut[i]) {
            for (std::size_t j : next[i]) {
                if (vxIn[j]) join(2 * i + 1, 2 * j);
            }
        }
    }
    std::vector<std::uint32_t> busy(nwords * 2);
    std::vector<unsigned> replacement(nwords * 2, 31);
    for (std::size_t i = 0; i < nwords; ++i) {
        if (!live[i]) continue;
        const std::uint32_t touched = decode(words[i]).used | in[i] | out[i];
        if (vxIn[i] || virtualTouch[i].use) busy[root(2 * i)] |= touched;
        if (vxOut[i] || virtualTouch[i].def) busy[root(2 * i + 1)] |= touched;
    }
    std::vector<std::uint8_t> spanSub(nwords, 0xFFu);
    for (const Ref& r : refs) {
        if (((words[r.idx] >> r.pos) & 31) != kX18) continue;
        const std::size_t group = root(2 * r.idx + (r.isDef ? 1 : 0));
        unsigned& sub = replacement[group];
        if (sub == 31) sub = pickFreeReg(busy[group]);
        if (sub == 31) {
            ++st.skippedNoReg;
            if (why) *why = "noreg";
            return -1;
        }
        spanSub[r.idx] = static_cast<std::uint8_t>(sub);
    }
    // Pass 2: patch every recorded field holding 18 (reachable code only).
    long sites = 0;
    for (std::size_t i = 0; i < nwords; ++i) {
        if (!live[i]) continue;
        const std::uint8_t sub = spanSub[i];
        if (sub == 0xFFu) continue;  // no x18 span covers this word
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

// Collect only reachable instruction sites; executable segments also contain data.
void collectVirtualSites(const std::uint32_t* code, std::size_t nwords,
                         std::size_t imageOffset, std::vector<std::size_t>& sites) {
    std::vector<char> seen(nwords);
    std::vector<std::size_t> work{0};
    std::vector<std::size_t> collected;
    bool computedBranch = false;

    // Returns false when the function cannot be stubbed at all: a virtual x18 lives
    // only in the thread slot, so a function is either stubbed completely or not.
    static const bool traceVirtual = [] {
        const char* v = std::getenv("KUDROID_X18_TRACE");
        return v != nullptr && v[0] == '1';
    }();
    auto fail = [&](std::size_t i, const char* why) {
        if (traceVirtual)
            std::fprintf(stderr, "[KuDroidELF] x18 virtual abort %s word=%08x at=%zx\n", why,
                         code[i], imageOffset + i * 4);
        return false;
    };
    auto consider = [&](std::size_t i) {
        const Decoded d = decode(code[i]);
        bool patchable = false;
        for (unsigned k = 0; k < d.npatch; ++k) {
            patchable |= ((code[i] >> d.at[k]) & 31u) == kX18;
        }
        if (!d.known && maybeX18(code[i])) return fail(i, "unknown");
        for (unsigned k = 0; k < d.nuse; ++k) {
            if (((code[i] >> d.useonly[k]) & 31u) == kX18) return fail(i, "opaque");
        }
        if (patchable) {
            // Calls and stack writeback need more than a single-instruction stub.
            // `br x18` has its own stub (load the virtual value, jump); every other
            // branch through the register is a call or authenticated form.
            const bool plainBrX18 = (code[i] & 0xFFFFFC1Fu) == 0xD61F0000u &&
                                    ((code[i] >> 5) & 31u) == kX18;
            if (!plainBrX18 && ((code[i] >> 24) == 0xD6 || (code[i] >> 24) == 0xD7))
                return fail(i, "branch");
            const unsigned rn = (code[i] >> 5) & 31u;
            const unsigned rd = code[i] & 31u;
            const unsigned b = code[i] >> 24;
            if (rd == 31 && (b == 0x91 || b == 0xD1)) return fail(i, "sp-arith");
            if (rn == 31 && (b & 0x3B) == 0x38 && !(code[i] & (1u << 21))) {
                const int offset = static_cast<std::int32_t>(code[i] << 11) >> 23;
                if (((code[i] >> 10) & 3u) != 0 || offset < 0) return fail(i, "sp-writeback");
            }
            if (rn == 31 && (b & 0x3E) == 0x28) {
                if (((code[i] >> 23) & 3u) != 2 || (code[i] & (1u << 21))) return fail(i, "sp-pair");
            }
            if (rn == 31 && ((b & 0xBF) == 0x0C || (b & 0xBF) == 0x0D)) return fail(i, "sp-simd");
            collected.push_back(imageOffset + i * 4);
        }
        return true;
    };

    while (!work.empty()) {
        const std::size_t i = work.back();
        work.pop_back();
        if (i >= nwords || seen[i]) continue;
        seen[i] = 1;
        if (!consider(i)) return;
        // A jump through a register (switch table) reaches blocks no direct edge
        // names; those blocks read the same virtual x18 and need the same stubs.
        if ((code[i] & 0xFFFFFC1Fu) == 0xD61F0000u && ((code[i] >> 5) & 31u) != 30u) {
            computedBranch = true;
        }
        if (hasCall(code[i])) {
            if (i + 1 < nwords) work.push_back(i + 1);
        } else {
            std::vector<std::size_t> successors;
            succEdge(code, nwords, i, successors, true, nullptr);
            work.insert(work.end(), successors.begin(), successors.end());
        }
    }
    if (computedBranch) {
        // Switch tables live in read-only data, not in the text, so every word left
        // over here is an unreachable-by-edge instruction of this function. Any that
        // touches x18 must be stubbed too; one that cannot be aborts the function.
        for (std::size_t i = 0; i < nwords; ++i) {
            if (seen[i]) continue;
            const Touch t = touch18(code[i]);
            if (!t.use && !t.def) continue;
            if (!consider(i)) return;
        }
    }
    sites.insert(sites.end(), collected.begin(), collected.end());
}

// -----------------------------------------------------------------------------
// Virtual x18 stubs for ranges without a safe replacement register.
// -----------------------------------------------------------------------------

constexpr size_t kStubBytes = 64;

#if defined(__APPLE__) && defined(__aarch64__)
static int64_t g_vx18_off = -1;
static std::once_flag g_vx18_once;
static bool vx18_init() {
    std::call_once(g_vx18_once, [] {
        pthread_key_t key;
        if (pthread_key_create(&key, nullptr) != 0) return;
        const uintptr_t sentinel = 0x5a5a1234deadbeefull;
        if (pthread_setspecific(key, reinterpret_cast<void*>(sentinel)) != 0) {
            pthread_key_delete(key);
            return;
        }
        uintptr_t base;
        __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(base));
        const auto* tsd = reinterpret_cast<const volatile uintptr_t*>(base & ~uintptr_t(7));
        int64_t off = -1;
        for (int i = 0; i < 520; ++i) {
            if (tsd[i] == sentinel) { off = static_cast<int64_t>(i * 8); break; }
        }
        pthread_setspecific(key, nullptr);
        if (off < 0 || off > 32760) { pthread_key_delete(key); return; }
        g_vx18_off = off;
    });
    return g_vx18_off >= 0;
}
#else
static int64_t g_vx18_off = 32;
static thread_local uint64_t g_fallback_vx18 = 0;
static bool vx18_init() {
    return true;
}
#endif

static inline uint32_t e_stur(unsigned rt, int imm) {
    return 0xF8000000u | ((static_cast<uint32_t>(imm) & 0x1FFu) << 12) | (31u << 5) | rt;
}
static inline uint32_t e_ldur(unsigned rt, int imm) {
    return 0xF8400000u | ((static_cast<uint32_t>(imm) & 0x1FFu) << 12) | (31u << 5) | rt;
}
static inline uint32_t e_mrs_tsd(unsigned rt) {
    return 0xD53BD060u | rt;
}
static inline uint32_t e_and_tsd(unsigned r) {
    return 0x927DF000u | (r << 5) | r;
}
static inline uint32_t e_ldr_slot(unsigned rt, unsigned rn, int64_t off) {
    return 0xF9400000u | ((static_cast<uint32_t>(off / 8) & 0xFFFu) << 10) | (rn << 5) | rt;
}
static inline uint32_t e_str_slot(unsigned rt, unsigned rn, int64_t off) {
    return 0xF9000000u | ((static_cast<uint32_t>(off / 8) & 0xFFFu) << 10) | (rn << 5) | rt;
}
static int e_mov64(uint32_t* out, unsigned rd, uint64_t v) {
    int n = 0;
    out[n++] = 0xD2800000u | (static_cast<uint32_t>(v & 0xFFFF) << 5) | rd;
    for (int sh = 1; sh < 4; ++sh) {
        uint64_t part = (v >> (16 * sh)) & 0xFFFF;
        if (part) out[n++] = 0xF2800000u | (static_cast<uint32_t>(sh) << 21) | (static_cast<uint32_t>(part) << 5) | rd;
    }
    return n;
}
static inline bool in_range_b(const uint8_t* from, const uint8_t* to) {
    int64_t o = (reinterpret_cast<int64_t>(to) - reinterpret_cast<int64_t>(from)) / 4;
    return o > -(1 << 25) && o < (1 << 25);
}
static inline uint32_t e_b(const uint8_t* from, const uint8_t* to) {
    return 0x14000000u | (static_cast<uint32_t>((reinterpret_cast<int64_t>(to) - reinterpret_cast<int64_t>(from)) / 4) & 0x03FFFFFFu);
}
static unsigned pick_scratch(uint32_t used, unsigned avoid) {
    static const unsigned order[] = {16, 17, 15, 14, 13, 12, 11, 10, 9};
    for (unsigned r : order) {
        if (!(used & (1u << r)) && r != avoid) return r;
    }
    return 31;
}
static uint32_t subst_x18(uint32_t insn, const Decoded& d, unsigned to) {
    uint32_t out = insn;
    for (unsigned k = 0; k < d.npatch; ++k) {
        unsigned pos = d.at[k];
        if (((out >> pos) & 31u) == kX18) {
            out = (out & ~(31u << pos)) | ((to & 31u) << pos);
        }
    }
    return out;
}

static bool emit_x18_stub(uint32_t insn, const Decoded& d, const uint8_t* pcExec,
                          uint8_t* stubWrite, const uint8_t* stubExec, size_t& stubUsed, size_t stubCap) {
    if (!vx18_init() || g_vx18_off < 0) return false;
    if (stubUsed + kStubBytes > stubCap) return false;

    uint8_t* rw = stubWrite + stubUsed;
    const uint8_t* rx = stubExec + stubUsed;
    if (!in_range_b(pcExec, rx)) return false;

    uint32_t c[kStubBytes / 4];
    int n = 0;
    const uint8_t* back = pcExec + 4;
    uint32_t used = d.used;

#define EMIT(w) do { if (n >= static_cast<int>(kStubBytes / 4)) return false; c[n++] = (w); } while(0)
#define EMIT_B(to) do { if (!in_range_b(rx + n * 4, (to))) return false; EMIT(e_b(rx + n * 4, (to))); } while(0)

    if ((insn & 0x9F000000u) == 0x90000000u || (insn & 0x9F000000u) == 0x10000000u || (insn & 0xBF000000u) == 0x18000000u) {
        uint64_t value = 0;
        bool literalLoad = false;
        if ((insn & 0x9F000000u) == 0x90000000u || (insn & 0x9F000000u) == 0x10000000u) {
            bool page = (insn & 0x80000000u) != 0;
            int64_t imm = static_cast<int64_t>((((insn >> 5) & 0x7FFFFu) << 2) | ((insn >> 29) & 3u));
            if (imm & 0x100000) imm -= 0x200000;
            uintptr_t tp = page ? (reinterpret_cast<uintptr_t>(pcExec) & ~static_cast<uintptr_t>(0xFFF)) + static_cast<uintptr_t>(imm * 4096)
                                : reinterpret_cast<uintptr_t>(pcExec) + static_cast<uintptr_t>(imm);
            value = tp;
        } else {
            int64_t imm = static_cast<int64_t>((insn >> 5) & 0x7FFFFu);
            if (imm & 0x40000) imm -= 0x80000;
            value = reinterpret_cast<std::uint64_t>(pcExec + imm * 4);
            literalLoad = true;
        }
        unsigned S = 16, T = 17;
        EMIT(e_stur(S, -16)); EMIT(e_stur(T, -8));
        n += e_mov64(c + n, S, value);
        if (literalLoad) {
            EMIT(((insn & 0x40000000u) ? 0xF9400000u : 0xB9400000u) | (S << 5) | S);
        }
        EMIT(e_mrs_tsd(T)); EMIT(e_and_tsd(T)); EMIT(e_str_slot(S, T, g_vx18_off));
        EMIT(e_ldur(T, -8)); EMIT(e_ldur(S, -16));
        EMIT_B(back);
    } else if ((insn & 0xFFFFFC1Fu) == 0xD61F0000u) {  // br x18
        EMIT(e_mrs_tsd(17)); EMIT(e_and_tsd(17)); EMIT(e_ldr_slot(17, 17, g_vx18_off));
        EMIT(0xD61F0000u | (17u << 5));
    } else if ((insn & 0x7E000000u) == 0x34000000u || (insn & 0x7E000000u) == 0x36000000u) {  // cbz/cbnz/tbz/tbnz
        bool is_tb = (insn & 0x7E000000u) == 0x36000000u;
        int64_t imm = is_tb ? static_cast<int64_t>((insn >> 5) & 0x3FFFu) : static_cast<int64_t>((insn >> 5) & 0x7FFFFu);
        int64_t sign = is_tb ? 0x2000 : 0x40000;
        if (imm & sign) imm -= sign * 2;
        const uint8_t* target = pcExec + imm * 4;
        unsigned S = 16;
        EMIT(e_stur(S, -16));
        EMIT(e_mrs_tsd(S)); EMIT(e_and_tsd(S)); EMIT(e_ldr_slot(S, S, g_vx18_off));
        uint32_t field_mask = is_tb ? (0x3FFFu << 5) : (0x7FFFFu << 5);
        EMIT((insn & ~(field_mask | 0x1Fu)) | (3u << 5) | S);
        EMIT(e_ldur(S, -16));
        EMIT_B(back);
        EMIT(e_ldur(S, -16));
        if (!in_range_b(rx + n * 4, target)) return false;
        EMIT_B(target);
    } else {
        unsigned S = pick_scratch(used, 31);
        unsigned T = pick_scratch(used, S);
        if (S >= 31 || T >= 31) return false;
        EMIT(e_stur(S, -16)); EMIT(e_stur(T, -8));
        EMIT(e_mrs_tsd(S)); EMIT(e_and_tsd(S)); EMIT(e_ldr_slot(S, S, g_vx18_off));
        EMIT(subst_x18(insn, d, S));
        EMIT(e_mrs_tsd(T)); EMIT(e_and_tsd(T)); EMIT(e_str_slot(S, T, g_vx18_off));
        EMIT(e_ldur(T, -8)); EMIT(e_ldur(S, -16));
        EMIT_B(back);
    }

#undef EMIT
#undef EMIT_B

    std::memcpy(rw, c, static_cast<size_t>(n) * 4);
    for (int i = n; i < static_cast<int>(kStubBytes / 4); ++i) {
        reinterpret_cast<uint32_t*>(rw)[i] = 0xD503201Fu;  // nop
    }
    stubUsed += kStubBytes;
    return true;
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
                         const std::uint8_t* fileData, std::size_t fileSize,
                         const void* execBase) {
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
    const auto* execImg = execBase != nullptr ? static_cast<const std::uint8_t*>(execBase) : img;
    // Executable sections (SHF_EXECINSTR), in file vaddr coordinates. .rodata and
    // .eh_frame often share the RX segment but are data.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> execSpans;
    if (shoff != 0 && shentsize >= 64 && shnum > 0 &&
        shoff + static_cast<std::uint64_t>(shnum) * shentsize <= fileSize) {
        for (std::uint16_t i = 0; i < shnum; ++i) {
            const std::uint8_t* e = fileData + shoff + i * shentsize;
            std::uint32_t type = 0;
            std::uint64_t flags = 0, addr = 0, size = 0;
            std::memcpy(&type, e + 4, 4);
            std::memcpy(&flags, e + 8, 8);
            std::memcpy(&addr, e + 16, 8);
            std::memcpy(&size, e + 32, 8);
            if (type == 8 /* SHT_NOBITS */ || (flags & 4) == 0 || size == 0) continue;
            execSpans.emplace_back(addr, addr + size);
        }
    }
    std::vector<std::size_t> virtualSites;
    (void)minVaddr;  // addressing is done in runtime coordinates throughout
    for (const auto& r : ranges) {
        if (r.end <= r.start || r.start < imgAddr) {
            ++st.skippedRange;
            continue;
        }
        if (r.personality) {
            ++st.skippedEh;
            static const bool traceEh = [] {
                const char* v = std::getenv("KUDROID_X18_TRACE");
                return v != nullptr && v[0] == '1';
            }();
            if (traceEh) {
                std::fprintf(stderr, "[KuDroidELF] x18 eh %llx-%llx\n",
                             static_cast<unsigned long long>(r.start - imgAddr),
                             static_cast<unsigned long long>(r.end - imgAddr));
            }
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
        } else {
            x18::collectVirtualSites(reinterpret_cast<const std::uint32_t*>(segBase + segOff),
                                     len / 4, static_cast<std::size_t>(r.start - imgAddr), virtualSites);
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
        auto processGap = [&](std::size_t gs, std::size_t ge) {
            if (ge <= gs + 4) return;  // need room for code, not noise
            if (ge > nwords || gs >= nwords) {
                std::fprintf(stderr,
                             "[KuDroidELF] gap OOB gs=%zu ge=%zu nwords=%zu "
                             "segv=%llx segfilesz=%zx ranges=%zu\n",
                             gs, ge, nwords, (unsigned long long)seg.vaddr,
                             static_cast<std::size_t>(seg.filesz),
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
            // Chunks are split at prologues and terminal flow, but a leaf routine has
            // no prologue, so a chunk can swallow a second routine that only a call
            // from elsewhere reaches. Its words look unreachable from the chunk start;
            // when one touches x18 the chunk is split after the terminal instruction
            // that precedes it and both halves are retried.
            std::vector<std::pair<std::size_t, std::size_t>> chunks;
            for (std::size_t s2 = splits.size() - 1; s2 > 0; --s2) {
                chunks.emplace_back(splits[s2 - 1], splits[s2]);
            }
            while (!chunks.empty()) {
                const std::size_t cs = chunks.back().first;
                const std::size_t ce = chunks.back().second;
                chunks.pop_back();
                if (ce <= cs + 1) continue;
                if (!x18::looksLikeCode(words + cs, ce - cs)) {
                    ++st.skippedRange;
                    continue;
                }
                const char* why0 = "";
                std::size_t dataAt = static_cast<std::size_t>(-1);
                X18Stats attempt = st;
                const long sites = x18::rewriteRange(
                    words + cs, ce - cs, attempt, &why0, words + gs, cs - gs,
                    ge - gs, false, &covered, &dataAt);
                if (sites <= 0 && dataAt != static_cast<std::size_t>(-1) && dataAt > 0) {
                    std::size_t cut = 0;
                    for (std::size_t i = dataAt; i > 0; --i) {
                        const std::uint32_t pw = words[cs + i - 1];
                        const unsigned pb = pw >> 24;
                        if ((pb == 0xD6 && ((pw >> 16) & 0xFF) != 0x3F) || (pb & 0xFC) == 0x14) {
                            cut = i;
                            break;
                        }
                    }
                    if (cut > 0 && cut < ce - cs) {
                        chunks.emplace_back(cs + cut, ce);
                        chunks.emplace_back(cs, cs + cut);
                        continue;
                    }
                }
                st = attempt;
                ++st.functions;
                if (sites > 0) {
                    ++st.rewritten;
                    st.sites += static_cast<std::uint64_t>(sites);
                } else {
                    x18::collectVirtualSites(words + cs, ce - cs,
                        static_cast<std::size_t>(seg.vaddr - minVaddr) + cs * 4, virtualSites);
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
        // Gaps are clipped to executable sections; without section headers the
        // whole run is used.
        auto flushGap = [&](std::size_t gs, std::size_t ge) {
            if (execSpans.empty()) {
                processGap(gs, ge);
                return;
            }
            for (const auto& span : execSpans) {
                if (span.second <= seg.vaddr || span.first >= seg.vaddr + seg.filesz) continue;
                const std::uint64_t a = std::max<std::uint64_t>(span.first, seg.vaddr + gs * 4);
                const std::uint64_t e = std::min<std::uint64_t>(span.second, seg.vaddr + ge * 4);
                if (e <= a) continue;
                processGap(static_cast<std::size_t>((a - seg.vaddr + 3) / 4),
                           static_cast<std::size_t>((e - seg.vaddr) / 4));
            }
        };
        for (const auto& c : covered) {
            if (c.first > cur) flushGap(cur, c.first);
            if (c.second > cur) cur = c.second;
        }
        if (cur < nwords) flushGap(cur, nwords);
    }

    // Redirect reachable x18 instructions through thread-local virtual state.
    struct StubPoolHolder {
        ExecMemory::Region region;
        size_t used = 0;
    };
    static std::mutex s_stubMtx;
    std::lock_guard<std::mutex> stubLock(s_stubMtx);
    StubPoolHolder pool;
    std::vector<std::pair<std::uint32_t*, std::uint32_t>> pending;
    auto commitPool = [&] {
        if (pending.empty()) return;
        if (!ExecMemory::Commit(pool.region, pool.region.writeView, pool.used)) {
            ExecMemory::BeginWrite();
            for (const auto& patch : pending) *patch.first = patch.second;
            st.stubbedSites -= pending.size();
        }
        ExecMemory::BeginWrite();
        pending.clear();
    };

    std::sort(virtualSites.begin(), virtualSites.end());
    virtualSites.erase(std::unique(virtualSites.begin(), virtualSites.end()), virtualSites.end());
    for (std::size_t offset : virtualSites) {
        auto* word = reinterpret_cast<std::uint32_t*>(const_cast<std::uint8_t*>(img) + offset);
        const std::uint32_t w = *word;
        const x18::Decoded d = x18::decode(w);
        const std::uint8_t* pcExec = execImg + offset;
        if (pool.region.writeView == nullptr ||
            pool.used + x18::kStubBytes > pool.region.size) {
            commitPool();
            pool.region = ExecMemory::Allocate(64 * 1024, true);
            pool.used = 0;
        }
        if (pool.region.writeView == nullptr) break;
        const std::size_t stubAt = pool.used;
        auto* stubWrite = static_cast<std::uint8_t*>(pool.region.writeView);
        const auto* stubExec = static_cast<const std::uint8_t*>(pool.region.execView);
        ExecMemory::BeginWrite();
        if (x18::emit_x18_stub(w, d, pcExec, stubWrite, stubExec,
                               pool.used, pool.region.size)) {
            pending.emplace_back(word, w);
            *word = x18::e_b(pcExec, stubExec + stubAt);
            ++st.stubbedSites;
        }
    }
    commitPool();

    return st;
}

TlsStubStats elf_tls_stub_rewrite(void* writeBase, const void* execBase, std::uint64_t minVaddr,
                                  const std::vector<ElfLoader::Segment>& segments,
                                  std::int64_t tsdSlotOffset, std::int64_t tlsPointerOffset) {
    TlsStubStats st;
#if defined(__APPLE__) && defined(__aarch64__)
    if (writeBase == nullptr || execBase == nullptr || tsdSlotOffset < 0 ||
        tsdSlotOffset > 32760 || (tsdSlotOffset & 7) != 0) {
        return st;
    }
    // `add xN, xN, #off`, built for N == 0 and given its register below.
    std::uint32_t addTemplate;
    if (tlsPointerOffset >= 0 && tlsPointerOffset < 4096) {
        addTemplate = 0x91000000u | (static_cast<std::uint32_t>(tlsPointerOffset) << 10);
    } else if (tlsPointerOffset > 0 && tlsPointerOffset % 4096 == 0 &&
               tlsPointerOffset / 4096 < 4096) {
        addTemplate = 0x91400000u |
                      (static_cast<std::uint32_t>(tlsPointerOffset / 4096) << 10);
    } else {
        return st;
    }

    struct Site {
        std::uint32_t* word;
        const std::uint8_t* exec;
        unsigned reg;
    };
    std::vector<Site> sites;
    for (const auto& seg : segments) {
        if ((seg.flags & 1) == 0) continue;
        const std::uint64_t rel = seg.vaddr - minVaddr;
        auto* words = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(writeBase) + rel);
        const auto* exec = static_cast<const std::uint8_t*>(execBase) + rel;
        const std::size_t n = static_cast<std::size_t>(seg.filesz) / 4;
        for (std::size_t i = 0; i < n; ++i) {
            if ((words[i] & 0xFFE0001Fu) != 0xD4200000u) continue;
            const unsigned imm = (words[i] >> 5) & 0xFFFFu;
            if (imm < 0x1000u || imm >= 0x1020u) continue;
            sites.push_back({&words[i], exec + i * 4, imm - 0x1000u});
        }
    }
    st.sites = sites.size();
    if (sites.empty()) return st;

    constexpr std::size_t kTlsStubBytes = 32;
    std::lock_guard<std::mutex> lock(s_tlsStubMutex());
    ExecMemory::Region region = ExecMemory::Allocate(sites.size() * kTlsStubBytes, true);
    if (region.writeView == nullptr || region.execView == nullptr ||
        region.size < sites.size() * kTlsStubBytes) {
        return st;
    }
    ExecMemory::BeginWrite();
    auto* stubWrite = static_cast<std::uint8_t*>(region.writeView);
    const auto* stubExec = static_cast<const std::uint8_t*>(region.execView);
    std::vector<std::pair<std::uint32_t*, std::uint32_t>> patched;
    std::size_t used = 0;
    for (const Site& site : sites) {
        const std::uint8_t* at = stubExec + used;
        if (!x18::in_range_b(site.exec, at) || !x18::in_range_b(at + 20, site.exec + 4) ||
            !x18::in_range_b(at + 28, site.exec + 4)) {
            ++st.outOfRange;
            continue;
        }
        const unsigned r = site.reg;
        std::uint32_t code[kTlsStubBytes / 4];
        code[0] = x18::e_mrs_tsd(r);
        code[1] = x18::e_and_tsd(r);
        code[2] = x18::e_ldr_slot(r, r, tsdSlotOffset);
        code[3] = 0xB4000000u | (3u << 5) | r;  // cbz xN, slow path
        code[4] = addTemplate | (r << 5) | r;
        code[5] = x18::e_b(at + 20, site.exec + 4);
        code[6] = 0xD4200000u | ((0x1000u + r) << 5);  // slow path: the trap handler fills xN
        code[7] = x18::e_b(at + 28, site.exec + 4);
        std::memcpy(stubWrite + used, code, sizeof(code));
        patched.emplace_back(site.word, *site.word);
        *site.word = x18::e_b(site.exec, at);
        used += kTlsStubBytes;
    }
    if (used == 0) return st;
    if (!ExecMemory::Commit(region, region.writeView, used)) {
        ExecMemory::BeginWrite();
        for (const auto& patch : patched) *patch.first = patch.second;
        return st;
    }
    ExecMemory::BeginWrite();
    st.stubbed = patched.size();
#else
    (void)writeBase; (void)execBase; (void)minVaddr; (void)segments;
    (void)tsdSlotOffset; (void)tlsPointerOffset;
#endif
    return st;
}

std::uint64_t elf_x18_get() {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!x18::vx18_init() || x18::g_vx18_off < 0) return 0;
    uintptr_t base;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(base));
    return *reinterpret_cast<const volatile uint64_t*>((base & ~static_cast<uintptr_t>(7)) + static_cast<uintptr_t>(x18::g_vx18_off));
#else
    return x18::g_fallback_vx18;
#endif
}

void elf_x18_set(std::uint64_t val) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!x18::vx18_init() || x18::g_vx18_off < 0) return;
    uintptr_t base;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(base));
    *reinterpret_cast<volatile uint64_t*>((base & ~static_cast<uintptr_t>(7)) + static_cast<uintptr_t>(x18::g_vx18_off)) = val;
#else
    x18::g_fallback_vx18 = val;
#endif
}

}  // namespace kudroid
