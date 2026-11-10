// Handlers that were installed before KuDroid's, per signal.
//
// A plain array indexed by signal number, not a map: this is read from a signal
// handler, where allocating or taking a lock is not allowed. NSIG covers every signal
// the platform defines.
static struct sigaction g_previousHandlers[NSIG];

// Hand a fault to whoever had the signal before us.
//
// Only reached for faults KuDroid does not own. Restoring the previous disposition and
// re-raising is what lets a chained handler see the ORIGINAL machine state: calling its
// function pointer directly would work for an SA_SIGINFO handler but not for a Mach
// exception port, and re-raising covers both.
static bool chainToPreviousHandler(int sig) {
    if (sig <= 0 || sig >= NSIG) return false;
    const struct sigaction& previous = g_previousHandlers[sig];
    const bool hasHandler =
        (previous.sa_flags & SA_SIGINFO) ? previous.sa_sigaction != nullptr
                                         : (previous.sa_handler != nullptr &&
                                            previous.sa_handler != SIG_DFL &&
                                            previous.sa_handler != SIG_IGN);
    if (!hasHandler) return false;
    sigaction(sig, &previous, nullptr);
    raise(sig);
    return true;
}


// ── AArch64 fault-instruction skip & worker resume ─────────────────────────
// A faulted worker parked forever deadlocks the engine: the main thread waits
// on the worker's job fence (JobHandle.Complete in libsystem cvwait) and the
// picture freezes with input dead. For a faulting LOAD/STORE whose semantics
// survive without the memory traffic, skipping the instruction lets the
// worker finish its job and wake the waiter:
//
//   load (LDR/LDP/LDRB/...): destination register(s) := 0, pc += 4.
//   store (STR/STP/...):     the write is dropped, pc += 4.
//
// Soundness rules (all checked before touching state):
// - SIGSEGV/SIGBUS only. Anything else parks.
// - pc must sit in a registered guest module: host text is never rewritten.
// - Only plain transfers: unsigned/unscaled immediate, register-offset,
//   literal loads, signed-offset pairs, PRFM, whole-register SIMD structure
//   transfers with no offset (LD1/ST1 x1-4, LD2/ST2, LD3/ST3, LD4/ST4, LD1R),
//   and SIMD pairs with no writeback (STP/LDP s/d/q). Pre/post-index
//   (writeback), exclusives, LSE atomics, SIMD lane-indexed forms, and
//   everything else park —
//   faking a base update or a synchronisation primitive corrupts silently,
//   while a skipped plain transfer only loses one value.
// - The computed effective address must equal si_addr: a decode that does
//   not explain the fault is a mis-decode, and those park.
// - Encoding shapes verified against aarch64-linux-gnu-as output, not the
//   ARM ARM from memory: b86d784c ldr w12,[x2,x13,lsl#2] allows,
//   a9c10440/a8c10440 ldp pre/post-index refuse, b8200041 ldadd refuses via
//   the bits[11:10]==10 rule (every LSE form carries bit21==1 with
//   bits[11:10] in {0,1,3}).
// - All async-signal-safe: scalar reads/writes, no locks, no allocation.
namespace kudroid {

bool fault_skip_branches_through(uint32_t nextWord, unsigned reg) {
    // 0xD6/0xD7 group with bits[24:21] == 0 is exactly the branch-to-register
    // family: BR, BLR, RET, ERET, DRPS and their authenticated variants. One
    // mask covers all of them, so a new pointer-auth form cannot slip past.
    if ((nextWord & 0xFE000000u) != 0xD6000000u) return false;
    // ERET/DRPS encode XZR in Rn; a fabricated destination is never 31 (the
    // decoder refuses rt >= 29), so they can never match a real skip.
    return ((nextWord >> 5) & 31u) == reg;
}

FaultSkipPlan fault_decode_skip(uint32_t w, uint64_t pc, uint64_t baseVal,
                                uint64_t rmVal) {
    FaultSkipPlan p;
    const unsigned top = w >> 24;
    const unsigned rt = (w >> 0) & 31;
    const unsigned rm = (w >> 16) & 31;

    if (top == 0x58 || top == 0xD8) {
        // LDR literal (32/64-bit): pc-relative, no writeback.
        const int32_t imm19 =
            static_cast<int32_t>((w >> 5) & 0x7FFFFu) << 13 >> 13;
        p.effAddr = pc + static_cast<int64_t>(imm19) * 4;
        p.isLoad = true;
        p.rt = rt;
        p.skippable = true;
        return p;
    }
    if ((top & 0x3B) == 0x38 || (top & 0x3B) == 0x39) {
        // Structural exclusion, not assembler luck: every atomic/exclusive/LSE
        // form (LDAR/STLR/LDAXP/STLXP/LDADD/SWP/CAS/CASP/LDAPR) carries
        // bits[29:24] in {001000, 011100}, i.e. bit27==0, while this branch
        // requires bits29,28,27==1. The check below makes the invariant
        // explicit so a future mask edit cannot silently admit an RMW into
        // the load-zero/store-drop path (faking an atomic is silent
        // corruption, worse than the crash).
        const unsigned major = (w >> 24) & 0x3F;
        if (major == 0x08 || major == 0x1C) return p;  // exclusive/LSE, LDAPR
        // Single transfer. SIMD/FP lanes (bit26) share the encoding with
        // integer lanes: Rt names a vector register (zeroed in __ns), Rn
        // stays an integer base, and the addressing modes compute
        // identically. Only the data-size rule differs (opc bit23 set =
        // 128-bit Q register, else 8<<size). SIMD lane-indexed forms stay
        // refused — lanes, not whole registers, fault there and zeroing them
        // is not semantics-preserving. Whole-register structure and pair
        // forms are decoded in their own branches below.
        const bool simd = (w & (1u << 26)) != 0;
        // PRFM: refused, not skipped. It is a hint with no destination, so a
        // skip would be harmless — but a faulting PRFM means its address is
        // bad, and silently advancing hides a real problem the very next
        // instruction will trip on anyway with a full report.
        if (((w >> 23) & 0x1FF) == 0x1F3) {
            return p;
        }
        // LSE atomics never reach the bit21==0 path (assembler-verified:
        // every LSE single-transfer form carries bit21==1), so no opc guard
        // is needed here — and none could be both correct and complete.
        // (LSE is integer-only; the bits[11:10]==10 rule below excludes it
        // structurally for both lanes.)
        p.isLoad = ((w >> 22) & 1) != 0;
        p.isVector = simd;
        const unsigned opc = (w >> 22) & 3;
        const unsigned sizeLog = (w >> 30) & 3;
        // Integer: one scale. SIMD: Q (opc bit 2) is 16 bytes, otherwise
        // the lane width 8<<size. Assembler-verified across B/H/S/D/Q.
        const unsigned scale = simd ? ((opc & 2) ? 4 : sizeLog) : sizeLog;
        if ((w & (1u << 21)) != 0) {
            // Register offset: bits[11:10] must be 10. This one test excludes
            // every LSE atomic and exclusive (verified: all carry bit21==1
            // with bits[11:10] in {0,1,3}, never 2).
            if (((w >> 10) & 3) != 2) return p;
            if (rm > 30) return p;  // 31 is not a valid index register
            const unsigned option = (w >> 13) & 7;
            const unsigned amount = (w >> 12) & 1;
            uint64_t idx = 0;
            switch (option) {
                case 0x3: idx = rmVal; break;  // LSL
                case 0x2:                       // UXTW
                case 0x1: idx = rmVal & 0xFFFFFFFFu; break;
                case 0x6: idx = rmVal; break;  // UXTX
                case 0x7: idx = rmVal; break;  // SXTX
                case 0x5:                       // SXTW
                    idx = static_cast<uint64_t>(
                        static_cast<int64_t>(static_cast<int32_t>(rmVal & 0xFFFFFFFFu)));
                    break;
                default: return p;  // 0x0/0x4: not a plain index
            }
            const unsigned shift = amount ? scale : 0;
            p.effAddr = baseVal + (idx << shift);
        } else if (((w >> 24) & 1) != 0) {
            // Unsigned immediate: no writeback.
            const uint64_t imm12 = (w >> 10) & 0xFFFu;
            p.effAddr = baseVal + (imm12 << scale);
        } else {
            // 0x_8 group (bit24==0; the entry mask already excludes the
            // post/pre-index bit patterns, which live elsewhere): the low two
            // bits select the form. 00 = LDUR/STUR and 10 = LDTR/STTR are both
            // unscaled with no writeback (assembler-verified against capstone
            // across B/H/W/X). 01/11 are post/pre-index with a base writeback
            // that cannot be faked: refuse.
            const unsigned mode = (w >> 10) & 3;
            if (mode == 1 || mode == 3) return p;
            int32_t simm9 =
                static_cast<int32_t>(((w >> 12) & 0x1FFu) << 23) >> 23;
            p.effAddr = baseVal + static_cast<int64_t>(simm9);
        }
        p.rt = rt;
        p.skippable = true;
        return p;
    }
    if ((top & 0x3F) == 0x28 || (top & 0x3F) == 0x29 ||
        (top & 0x3F) == 0x2C || (top & 0x3F) == 0x2D) {
        // Integer pair, or SIMD&FP pair with bit26 set (STP/LDP s/d/q): two
        // explicit destinations, signed offset, no writeback. The vector
        // shape moves whole registers, so it zeroes like the integer pair.
        // Assembler-verified: ad7f5e55 ldp q21,q23,[x18,#-32] (live crash),
        // 2d408c02 ldp s2,s3,[x0,#4], 6d7f0c02 ldp d2,d3,[x0,#-16],
        // ad011404 stp q4,q5,[x0,#32], ad7e1fe6 ldp q6,q7,[sp,#-64].
        // Scale is opc+2 (S=4B, D=8B, Q=16B); opc==3 is unallocated.
        // Indexing lives in bits[24:23] verified against the assembler as
        // (w>>23)&3: 2 = signed offset, 3 = pre-index, 1 = post-index,
        // 0 = unallocated (ad808400 pre-index and acc10c02 post-index refuse).
        if (((w >> 23) & 3) != 2) return p;  // writeback/unallocated
        const bool simdPair = (w & (1u << 26)) != 0;
        unsigned scale;
        if (simdPair) {
            const unsigned opc = (w >> 30) & 3;
            if (opc == 3) return p;
            scale = opc + 2;
        } else {
            scale = ((w >> 31) & 1) ? 3 : 2;  // 64- vs 32-bit lanes
        }
        p.isPair = true;
        p.isLoad = ((w >> 22) & 1) != 0;
        p.rt = rt;
        p.rt2 = (w >> 10) & 31;
        p.isVector = simdPair;
        p.rtCount = 1;
        int32_t simm7 = static_cast<int32_t>(((w >> 15) & 0x7Fu) << 25) >> 25;
        p.effAddr = baseVal + (static_cast<int64_t>(simm7) << scale);
        p.skippable = true;
        return p;
    }
    if (top == 0x0C || top == 0x4C) {
        // AdvSIMD multiple structures, no offset (LD1/ST1 x1-4, LD2/ST2,
        // LD3/ST3, LD4/ST4): whole vector registers move, so a faulting one
        // zeroes (loads) or drops (stores) exactly like a scalar pair.
        // Assembler-verified: 0c008a5e st2 {v30.2s,v31.2s},[x18] (live
        // crash), 4c002800 st1 x4, 4c40a800 ld1 x2, 4c400800 ld4, 0c000800 st4.
        // Writeback forms carry the post-index register in bits[20:16]
        // (4cdf7800 ld1,[x0],#16 has Rm=31 there; 0c818800 st2,[x0],x1 has
        // Rm=1): a faked base update corrupts silently, so any nonzero there
        // refuses, same rule as scalar post/pre-index.
        if (((w >> 16) & 0x1F) != 0) return p;
        const unsigned opc = (w >> 12) & 0xF;
        unsigned n = 0;
        switch (opc) {
            case 0x7: n = 1; break;
            case 0xA: case 0x8: n = 2; break;
            case 0x6: case 0x4: n = 3; break;
            case 0x2: case 0x0: n = 4; break;
            default: return p;  // unallocated in the no-offset form
        }
        if (rt + n > 32) return p;  // register list must not wrap
        p.isLoad = ((w >> 22) & 1) != 0;
        p.isVector = true;
        p.rt = rt;
        p.rtCount = n;
        p.effAddr = baseVal;
        p.skippable = true;
        return p;
    }
    if (top == 0x0D || top == 0x4D) {
        // AdvSIMD single structure (LD1R plus one-lane LD1/ST1). Only LD1R
        // moves a whole register (replicated); lane forms touch part of one,
        // and zeroing the rest is not semantics-preserving, so they refuse.
        // LD1R-vs-lane verified against all 60 lane forms (every size, index,
        // LD+ST): none carries opcode 0xC, so it selects LD1R exactly.
        // Stores here are always lane forms (no ST1R exists): refuse.
        // Assembler-verified: 0d40c905 ld1r {v5.2s},[x8] allows; 4d408000
        // ld1 {v0.s}[2],[x0] and 4d004862 st1 {v2.h}[5],[x3] refuse.
        if (((w >> 22) & 1) == 0) return p;
        if (((w >> 12) & 0xF) != 0xC) return p;
        p.isLoad = true;
        p.isVector = true;
        p.rt = rt;
        p.rtCount = 1;
        p.effAddr = baseVal;
        p.skippable = true;
        return p;
    }
    return p;
}

}  // namespace kudroid

#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
namespace {

bool fault_skip_load_store(ucontext_t* uc, uintptr_t faultAddr, uint64_t* newPcOut,
                           bool* isLoadOut) {
    if (uc == nullptr || newPcOut == nullptr) return false;
    const uint64_t pc = uc->uc_mcontext->__ss.__pc;
    // Guest text only (see above). The lookup try-locks and gives up rather
    // than blocking, so it is safe here.
    char mod[256] = {0};
    if (!kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc), mod,
                                              sizeof(mod))) {
        return false;
    }
    const uint32_t w = *reinterpret_cast<const uint32_t*>(pc);
    const unsigned rn = (w >> 5) & 31;

    auto regVal = [&](unsigned r) -> uint64_t {
        if (r == 31) return 0;
        if (r == 30) return arm_thread_state64_get_lr(uc->uc_mcontext->__ss);
        if (r == 29) return arm_thread_state64_get_fp(uc->uc_mcontext->__ss);
        if (r > 28) return 0;
        return uc->uc_mcontext->__ss.__x[r];
    };
    const uint64_t baseVal =
        (rn == 31) ? arm_thread_state64_get_sp(uc->uc_mcontext->__ss) : regVal(rn);
    const uint64_t rmVal = regVal((w >> 16) & 31);

    const kudroid::FaultSkipPlan p = kudroid::fault_decode_skip(w, pc, baseVal, rmVal);
    if (!p.skippable) return false;
    // The decode must explain the fault. Anything else is a mis-decode.
    // Null-page imprecision: a faulting multi-register SIMD access in the
    // first page may report si_addr as the page rather than the faulting byte
    // (observed: st2 [x18=0x1f8] reported as 0x0). Inside the first page the
    // exact byte carries no safety signal — nothing is mapped there — so
    // same-page equality suffices; everywhere else it stays exact.
    if (p.effAddr != faultAddr &&
        (p.effAddr >= 0x1000u || faultAddr >= 0x1000u)) return false;
    // The fabricated result must not become a control-flow target. When the
    // word right after the faulting load branches through the register this
    // skip would zero, resuming there calls address 0 instead: the thread hops
    // to pc=0 and the report names that, not the load whose base was wrong
    // (observed live on a null-base load followed by `blr x8`). Refuse the
    // skip so the fault is reported where it happened. pc+4 is fetchable
    // whenever it stays inside pc's page, and pc is executing right now.
    if (p.isLoad) {
        if ((pc & 0xFFFu) + 4 < 0x1000u) {
            const uint32_t next = *reinterpret_cast<const uint32_t*>(pc + 4);
            unsigned via = 32;  // >= 32: no register this skip fabricates
            if (kudroid::fault_skip_branches_through(next, p.rt)) {
                via = p.rt;
            } else if (p.isPair && kudroid::fault_skip_branches_through(next, p.rt2)) {
                via = p.rt2;
            } else if (p.isVector) {
                for (unsigned i = 1; i < p.rtCount; ++i) {
                    if (kudroid::fault_skip_branches_through(next, p.rt + i)) {
                        via = p.rt + i;
                        break;
                    }
                }
            }
            if (via < 32) {
                char nb[192];
                const int cn = std::snprintf(
                    nb, sizeof(nb),
                    "null-probe skip refused: next insn branches through r%u, the "
                    "register the skip would fabricate",
                    via);
                if (cn > 0) kudroid_persistent_breadcrumb(nb);
                return false;
            }
        }
    }
    // Reached only for nullish faults, so the fabricated result below lands
    // in a register whose only sane value a null read could have produced anyway. Storm control stays in the per-thread budget.
    if (p.isLoad) {
        if (p.isVector) {
            // SIMD/FP lane: all 32 vector registers exist (no XZR), so no
            // destination gate is needed. Zero, like the scalar case below: a
            // null-page read is the only fault that reaches here, so the value
            // the guest would have seen is zero bytes. A poison pattern is
            // worse, not better -- 0xCD bytes reinterpreted as four pointers
            // are four wild addresses. Structure loads zero every consecutive
            // destination (rtCount); a pair's second dest is explicit, not
            // rt+1 (observed: ldp q21,q23). The decoder already refused a
            // wrapping structure list; 5-bit pair fields cannot wrap.
            if (p.rt > 31 || p.rtCount < 1 || p.rtCount > 4 ||
                p.rt + p.rtCount > 32) return false;
            if (p.isPair && p.rt2 > 31) return false;
            for (unsigned i = 0; i < p.rtCount; ++i) {
                std::memset(&uc->uc_mcontext->__ns.__v[p.rt + i], 0,
                            sizeof(uc->uc_mcontext->__ns.__v[0]));
            }
            if (p.isPair) {
                std::memset(&uc->uc_mcontext->__ns.__v[p.rt2], 0,
                            sizeof(uc->uc_mcontext->__ns.__v[0]));
            }
        } else {
            // 29/30 never take a transfer result in valid code, 31 is XZR
            // (no effect): refuse rather than reason about them.
            if (p.rt >= 29 || (p.isPair && p.rt2 >= 29)) return false;
            // 18 is the platform register: guest code keeps live addresses in it
            // across calls (a compiler that reserves it treats it as callee-saved),
            // and a fabricated zero is stored into structures and dereferenced far
            // away from here. Observed live: a job loop stored x18 into a record,
            // then dereferenced that field at +0x28 in the next statement
            // (fault_addr=0x28, x18=0 in the dump). Inventing a value for the
            // register the guest cannot re-derive is not a recovery, it is a
            // delayed crash with no connection to the fault; report the real one.
            if (p.rt == 18 || (p.isPair && p.rt2 == 18)) {
                static std::atomic<int> s_r18{0};
                if (s_r18.fetch_add(1, std::memory_order_relaxed) < 4) {
                    char nb[192];
                    const int cn = std::snprintf(
                        nb, sizeof(nb),
                        "skip refused: fabricated destination is the platform "
                        "register (pc=0x%llx fault_addr=0x%llx)",
                        static_cast<unsigned long long>(pc),
                        static_cast<unsigned long long>(faultAddr));
                    if (cn > 0) kudroid_persistent_breadcrumb(nb);
                }
                return false;
            }
            // Zero, not a poison value: the register result feeds straight
            // into later pointer arithmetic, and a poison like 0xDEAD turned
            // "base + offset" into an unmapped address a few instructions
            // later (observed live: x12=0xdead then SIGBUS on x14 load in a
            // Unity Job.Worker). Zero is what the hardware faults read as on
            // a null page, which is the semantics a faulting load leaves
            // behind — an engine null-check then treats it as the missing
            // data it already handles everywhere else.
            uc->uc_mcontext->__ss.__x[p.rt] = 0;
            if (p.isPair) uc->uc_mcontext->__ss.__x[p.rt2] = 0;
        }
    }
    *newPcOut = pc + 4;
    if (isLoadOut != nullptr) *isLoadOut = p.isLoad;
    return true;
}

}  // namespace

// Steady-clock nanoseconds: signal-safe, and the clock every window and throttle
// in this file already reads.
static long long fault_steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Append one line to <logDir>/kudroid_crash.log. Async-signal-safe: open/write/
// close only, no heap. The fault-skip mirror and the episode report below share
// it so a recovered fault and the fault it led to land in the same file.
static void crashAppendLogLine(const char* line, int n) {
    if (line == nullptr || n <= 0 || !g_logDir[0]) return;
    char path[1200];
    size_t dl = std::strlen(g_logDir);
    if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
    std::memcpy(path, g_logDir, dl);
    const char* suffix = "/kudroid_crash.log";
    std::memcpy(path + dl, suffix, std::strlen(suffix) + 1);
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    (void)!::write(fd, line, static_cast<size_t>(n));
    ::close(fd);
}

// Last dropped store per thread. A store drop changes no register and no memory,
// so it leaves no trace in the guest's state: without a note here a fault the
// drop may have caused one iteration later reads as an unrelated crash.
// One slot per thread, atomics only (signal context: no locks, no alloc).
struct StoreDropSlot {
    std::atomic<unsigned long long> tid{0};
    std::atomic<unsigned long long> pc{0};
    std::atomic<long long> ns{0};
};
static StoreDropSlot g_storeDrops[8];

// How far and how late the follow-up fault may arrive and still count as the
// same loop. A broken loop is small and its iteration is quick; the bounds admit
// a slow body while a pc 256 bytes away is a different code path.
static constexpr unsigned long long kStoreDropPcRange = 256;
static constexpr long long kStoreDropEpisodeNs = 100000000LL;  // 100 ms

static void store_drop_note(unsigned long long tid, unsigned long long pc) {
    if (tid == 0 || pc == 0) return;
    const long long now = fault_steady_ns();
    for (auto& s : g_storeDrops) {
        if (s.tid.load(std::memory_order_relaxed) == tid) {
            s.pc.store(pc, std::memory_order_relaxed);
            s.ns.store(now, std::memory_order_relaxed);
            return;
        }
    }
    for (auto& s : g_storeDrops) {
        unsigned long long z = 0;
        if (s.tid.compare_exchange_strong(z, tid, std::memory_order_relaxed)) {
            s.pc.store(pc, std::memory_order_relaxed);
            s.ns.store(now, std::memory_order_relaxed);
            return;
        }
    }
}

// Report a dropped store and a fault that followed it from the same loop as one
// episode, at most once: the slot is cleared whether or not it matched, so a
// drop with no follow-up costs nothing and a storm cannot repeat the line.
static void store_drop_report(unsigned long long tid, unsigned long long pc,
                              unsigned long long faultAddr) {
    if (tid == 0) return;
    for (auto& s : g_storeDrops) {
        if (s.tid.load(std::memory_order_relaxed) != tid) continue;
        const unsigned long long dropPc = s.pc.load(std::memory_order_relaxed);
        const long long dropNs = s.ns.load(std::memory_order_relaxed);
        s.tid.store(0, std::memory_order_relaxed);
        s.pc.store(0, std::memory_order_relaxed);
        s.ns.store(0, std::memory_order_relaxed);
        if (dropPc == 0 || pc == 0) return;
        const unsigned long long gap = pc > dropPc ? pc - dropPc : dropPc - pc;
        if (gap > kStoreDropPcRange) return;
        const long long now = fault_steady_ns();
        if (dropNs == 0 || now - dropNs > kStoreDropEpisodeNs) return;
        char mark[256];
        const int n = std::snprintf(
            mark, sizeof(mark),
            "store-drop episode: dropped store pc=0x%llx, next fault pc=0x%llx "
            "fault_addr=0x%llx pc_gap=%lluB after=%lldus\n",
            dropPc, pc, faultAddr, gap, (now - dropNs) / 1000);
        if (n > 0) {
            kudroid_persistent_breadcrumb(mark);
            crashAppendLogLine(mark, n);
        }
        return;
    }
}

static bool kudroid_try_skip_fault(int /*sig*/, siginfo_t* info, void* ucontext) {
    if (info == nullptr || ucontext == nullptr) return false;
    ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext);
    uint64_t newPc = 0;
    bool isLoad = false;
    // Read before the resume pc is written: the drop note below names the
    // instruction that faulted, not the one execution resumed at.
    const uint64_t faultPc = uc->uc_mcontext->__ss.__pc;
    const uintptr_t faultAddr = reinterpret_cast<uintptr_t>(info->si_addr);
    // A fault may only be silently recovered in two shapes.
    //
    // Null-page probe (loads and stores below the first page): the guest
    // dereferences a pointer whose null check it got wrong, and inventing a
    // result cannot corrupt live state because nothing the guest owns lives
    // there — or just below zero, for a null base with a negative structure
    // offset. Anywhere mapped, fabricating a load result feeds garbage into
    // the structures the guest is walking — observed live as a hash-table loop
    // that stored a poisoned SIMD lane (0xCD) into a live array, kept walking
    // with the poisoned index and finally wrote 33GB off the heap.
    //
    // Unmapped store drop: a STORE whose fault address has no mapping at all,
    // outside the thread's own stack and the null page. Dropping the write
    // fabricates nothing — no register changes, memory keeps its previous
    // bytes — so no readable state is poisoned. The cost is one lost update
    // (a worker's collision-index store), against the alternative: the whole
    // engine dying on a fault whose recovery is provably inert. Observed
    // 2026-09-26: Job.Worker 1 took SEGV_ACCERR at 0x716c2ec7b4, region <none>,
    // plan decoded, recovery refused on the nullish gate only; the game died
    // 2m16s into the run. Decoding, branch and register guards still apply
    // below; atomics/exclusives never decode as skippable.
    const bool storeDropEligible =
        !fault_addr_is_nullish(faultAddr) && unmapped_addr_is_store_safe(faultAddr);
    if (!fault_addr_is_nullish(faultAddr) && !storeDropEligible) return false;
    if (!fault_skip_load_store(uc, faultAddr, &newPc, &isLoad)) return false;
    // Allow unmapped loads when the instruction immediately following the load
    // is a conditional zero-test/branch (tbz/cbz/tbnz/cbnz) on the destination register.
    bool loadZeroTestSafe = false;
    if (!fault_addr_is_nullish(faultAddr) && isLoad && (faultPc & 0xFFFu) + 4 < 0x1000u) {
        const uint32_t currentInsn = *reinterpret_cast<const uint32_t*>(faultPc);
        const unsigned rt = currentInsn & 31;
        const uint32_t next = *reinterpret_cast<const uint32_t*>(faultPc + 4);
        if (((next & 0x7e000000) == 0x36000000 || (next & 0x7e000000) == 0x34000000) &&
            (next & 31) == rt) {
            loadZeroTestSafe = true;
        }
    }
    if (!fault_addr_is_nullish(faultAddr) && isLoad && !loadZeroTestSafe) return false;
    arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss,
                                   reinterpret_cast<void*>(newPc));
    char mark[256];
    const int n = snprintf(mark, sizeof(mark),
                           "fault-skipped kind=%s fault_addr=0x%llx resumed_at_pc=0x%llx",
                           (fault_addr_is_nullish(faultAddr) ? "null-page"
                                                             : (loadZeroTestSafe ? "load-zerotest" : "store-unmapped")),
                           (unsigned long long)faultAddr,
                           (unsigned long long)newPc);
    if (n > 0) kudroid_persistent_breadcrumb(mark);
    // Recovered faults otherwise live only in native_breadcrumbs.log, so a storm that
    // eventually exhausts the per-thread budget looks like a single fatal fault in the
    // primary crash log (observed: 128 silent skips, then one fatal load in a Unity Job
    // worker). Mirror the first few and every 16th to kudroid_crash.log so the run-up
    // is visible where it is actually read. Async-signal-safe: open/write/close only.
    {
        static std::atomic<unsigned> s_skipLog{0};
        const unsigned seen = s_skipLog.fetch_add(1, std::memory_order_relaxed);
        if (seen < 8 || (seen % 16) == 0) {
            if (g_logDir[0]) {
                // Which mapping the fault came from decides the fix: a region
                // that exists with insufficient cur protection is a commit we
                // failed to make, while no region at all (or one whose max
                // protection cannot grant the access) is a wild address.
                char reg[160];
                {
                    char cur[4] = "???", max[4] = "???";
                    uint64_t rbase = 0, rsize = 0;
                    // A lookup that returns a region the address is not inside
                    // is not the fault's region: for an address below the first
                    // mapping the kernel answers with the first region in the
                    // space (observed: fault_addr=0x28 reported as a region at
                    // 0x100850000). Reporting that as "the region" reads as a
                    // committed-but-unprotected page and sends the next
                    // investigation the wrong way, so require containment.
                    if (kudroid::QueryRegionProt(
                            reinterpret_cast<const void*>(faultAddr), cur, max,
                            &rbase, &rsize) &&
                        faultAddr >= rbase && faultAddr < rbase + rsize) {
                        std::snprintf(reg, sizeof(reg),
                                      " region=0x%llx+0x%llx cur=%s max=%s",
                                      (unsigned long long)rbase,
                                      (unsigned long long)rsize, cur, max);
                    } else {
                        std::snprintf(reg, sizeof(reg), " region=<none>");
                    }
                }
                char rec[480];
                const int m = std::snprintf(rec, sizeof(rec),
                                            "[fault-skip #%u] %s kind=%s%s\n",
                                            seen + 1, mark,
                                            isLoad ? "load" : "store", reg);
                if (m > 0) crashAppendLogLine(rec, m);
            }
        }
    }
    // A store drop is the one recovery that changes neither register nor memory,
    // so this note is its only trace: it lets crashHandler report a fault
    // arriving from the same loop as the pair it is (see store_drop_report).
    if (!fault_addr_is_nullish(faultAddr) && !isLoad) {
        store_drop_note(currentThreadIdForCrash(), faultPc);
    }
    return true;
}
#else
// Non-Apple or non-arm64 hosts never run guest AArch64 text in-process:
// every worker fault parks.
static bool kudroid_try_skip_fault(int, siginfo_t*, void*) { return false; }
#endif

// SIGUSR2 interrupter for the fatal-path park below. Empty on purpose: its job
// is turning the default-terminate disposition into EINTR, so a main thread
// blocked in the engine wait returns to its loop, where the shell poll sees
// g_hasCrashed and shows the modal. Safe in a handler by doing nothing.
static void kudroid_wake_handler(int) {}

#if defined(__APPLE__) && defined(__arm64__)
// Fault context for a fatal hardware fault: general registers, the thread's
// stack bounds, the region of the fault address, and for each register that
// points into readable memory a short peek at the pointee. Signal-safe: only
// snprintf, write, and vm_read_overwrite (which fails instead of faulting).
static void log_fault_context(const void* ucontext, const void* faultAddr) {
    if (ucontext == nullptr) return;
    const ucontext_t* uc = static_cast<const ucontext_t*>(ucontext);
    const auto& ss = uc->uc_mcontext->__ss;
    char line[400];
    for (int base = 0; base < 29; base += 4) {
        int n = std::snprintf(line, sizeof(line), "fault-regs");
        for (int i = base; i < base + 4 && i < 29; ++i) {
            n += std::snprintf(line + n, sizeof(line) - static_cast<size_t>(n),
                               " x%d=0x%llx", i,
                               static_cast<unsigned long long>(ss.__x[i]));
        }
        signal_safe_log("KuDroidTrap", line);
    }
    const kudroid::StackBounds st = kudroid::query_thread_stack_bounds();
    char cur[4] = "???", max[4] = "???";
    uint64_t rbase = 0, rsize = 0;
    const bool haveRegion = kudroid::QueryRegionProt(faultAddr, cur, max, &rbase, &rsize);
    std::snprintf(line, sizeof(line),
                  "fault-ctx sp=0x%llx fp=0x%llx stack=[0x%llx,0x%llx) valid=%d "
                  "region=%s [0x%llx,0x%llx) prot=%s/%s",
                  static_cast<unsigned long long>(ss.__sp),
                  static_cast<unsigned long long>(ss.__fp),
                  static_cast<unsigned long long>(st.low),
                  static_cast<unsigned long long>(st.high), st.valid ? 1 : 0,
                  haveRegion ? "yes" : "none",
                  static_cast<unsigned long long>(rbase),
                  static_cast<unsigned long long>(rbase + rsize), cur, max);
    signal_safe_log("KuDroidTrap", line);
    for (int i = 0; i < 29; ++i) {
        const uint64_t p = ss.__x[i];
        if (p < 0x100000000ULL || (p & 7) != 0) continue;
        for (unsigned offset : {0u, 64u}) {
            if (p > UINT64_MAX - offset) continue;
            uint64_t w[12];
            vm_size_t got = 0;
            if (vm_read_overwrite(mach_task_self(), p + offset, sizeof(w),
                                  reinterpret_cast<vm_address_t>(w), &got) != KERN_SUCCESS ||
                got != sizeof(w)) {
                continue;
            }
            std::snprintf(line, sizeof(line),
                          "fault-peek x%d+%u: %016llx %016llx %016llx %016llx %016llx %016llx "
                          "%016llx %016llx %016llx %016llx %016llx %016llx",
                          i, offset, static_cast<unsigned long long>(w[0]), static_cast<unsigned long long>(w[1]),
                          static_cast<unsigned long long>(w[2]), static_cast<unsigned long long>(w[3]),
                          static_cast<unsigned long long>(w[4]), static_cast<unsigned long long>(w[5]),
                          static_cast<unsigned long long>(w[6]), static_cast<unsigned long long>(w[7]),
                          static_cast<unsigned long long>(w[8]), static_cast<unsigned long long>(w[9]),
                          static_cast<unsigned long long>(w[10]), static_cast<unsigned long long>(w[11]));
            signal_safe_log("KuDroidTrap", line);
        }
    }
}
#endif

