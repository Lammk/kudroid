#include "kudroid/kuart/JitCompiler.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "dex/code_item_accessors-inl.h"
#include "dex/dex_instruction.h"
#include "dex/dex_instruction-inl.h"

#include "kudroid/kuart/JitCache.h"

namespace kudroid {
namespace kuart {

using art::Instruction;

namespace {

std::atomic<size_t> g_compiled{0};
std::atomic<size_t> g_refused{0};

// Fixed registers: x0-x2 are live throughout; scratch starts at x9.
constexpr int kRegs = 0;      // x0: DexValue* registers
constexpr int kResult = 1;    // x1: DexValue* out_result
constexpr int kOutPc = 2;     // x2: uint32_t* out_pc
constexpr int kTmpA = 9;
constexpr int kTmpB = 10;
constexpr int kTmpC = 11;
constexpr int kZR = 31;

// Byte offset of a DEX virtual register inside the frame's array.
uint32_t VRegOffset(uint32_t vreg) { return vreg * static_cast<uint32_t>(sizeof(DexValue)); }

}  // namespace

// Encoder: encodings are pinned by the golden-byte test.

void JitCompiler::Asm::LdrX(int rt, int rn, uint32_t byteOffset) {
    // LDR (immediate, unsigned offset), 64-bit: 1111 1001 01ii iiii iiii iinn nnnt tttt
    // imm12 is scaled by 8, so the offset must be a multiple of 8 and < 32768.
    const uint32_t imm12 = (byteOffset / 8) & 0xFFF;
    Emit(0xF9400000u | (imm12 << 10) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rt));
}

void JitCompiler::Asm::StrX(int rt, int rn, uint32_t byteOffset) {
    const uint32_t imm12 = (byteOffset / 8) & 0xFFF;
    Emit(0xF9000000u | (imm12 << 10) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rt));
}

void JitCompiler::Asm::LdrW(int rt, int rn, uint32_t byteOffset) {
    // 32-bit form scales imm12 by 4.
    const uint32_t imm12 = (byteOffset / 4) & 0xFFF;
    Emit(0xB9400000u | (imm12 << 10) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rt));
}

void JitCompiler::Asm::StrW(int rt, int rn, uint32_t byteOffset) {
    const uint32_t imm12 = (byteOffset / 4) & 0xFFF;
    Emit(0xB9000000u | (imm12 << 10) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rt));
}

void JitCompiler::Asm::MovzW(int rd, uint16_t imm) {
    Emit(0x52800000u | (static_cast<uint32_t>(imm) << 5) | static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MovnW(int rd, uint16_t imm) {
    Emit(0x12800000u | (static_cast<uint32_t>(imm) << 5) | static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MovkW(int rd, uint16_t imm, int shift) {
    // hw field selects the 16-bit lane: shift/16.
    const uint32_t hw = (static_cast<uint32_t>(shift) / 16u) & 0x1u;
    Emit(0x72800000u | (hw << 21) | (static_cast<uint32_t>(imm) << 5) |
         static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MovzX(int rd, uint16_t imm) {
    Emit(0xD2800000u | (static_cast<uint32_t>(imm) << 5) | static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MovkX(int rd, uint16_t imm, int shift) {
    const uint32_t hw = (static_cast<uint32_t>(shift) / 16u) & 0x3u;
    Emit(0xF2800000u | (hw << 21) | (static_cast<uint32_t>(imm) << 5) |
         static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MovX(int rd, int rm) {
    // mov xD, xM is ORR xD, xzr, xM.
    Emit(0xAA0003E0u | (static_cast<uint32_t>(rm) << 16) | static_cast<uint32_t>(rd));
}

// Three-register data processing. `base` carries opcode + size bit.
static uint32_t Enc3(uint32_t base, int rd, int rn, int rm) {
    return base | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5) |
           static_cast<uint32_t>(rd);
}

void JitCompiler::Asm::AddW(int rd, int rn, int rm) { Emit(Enc3(0x0B000000u, rd, rn, rm)); }
void JitCompiler::Asm::SubW(int rd, int rn, int rm) { Emit(Enc3(0x4B000000u, rd, rn, rm)); }
void JitCompiler::Asm::AddX(int rd, int rn, int rm) { Emit(Enc3(0x8B000000u, rd, rn, rm)); }
void JitCompiler::Asm::SubX(int rd, int rn, int rm) { Emit(Enc3(0xCB000000u, rd, rn, rm)); }

void JitCompiler::Asm::MulW(int rd, int rn, int rm) {
    // MADD with Ra = zr.
    Emit(0x1B007C00u | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MulX(int rd, int rn, int rm) {
    Emit(0x9B007C00u | (static_cast<uint32_t>(rm) << 16) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::SdivW(int rd, int rn, int rm) { Emit(Enc3(0x1AC00C00u, rd, rn, rm)); }
void JitCompiler::Asm::SdivX(int rd, int rn, int rm) { Emit(Enc3(0x9AC00C00u, rd, rn, rm)); }

void JitCompiler::Asm::MsubW(int rd, int rn, int rm, int ra) {
    Emit(0x1B008000u | (static_cast<uint32_t>(rm) << 16) |
         (static_cast<uint32_t>(ra) << 10) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::MsubX(int rd, int rn, int rm, int ra) {
    Emit(0x9B008000u | (static_cast<uint32_t>(rm) << 16) |
         (static_cast<uint32_t>(ra) << 10) | (static_cast<uint32_t>(rn) << 5) |
         static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::NegW(int rd, int rm) {
    // neg wD, wM is SUB wD, wzr, wM.
    Emit(Enc3(0x4B000000u, rd, kZR, rm));
}

void JitCompiler::Asm::NegX(int rd, int rm) {
    Emit(Enc3(0xCB000000u, rd, kZR, rm));
}
void JitCompiler::Asm::MvnW(int rd, int rm) { Emit(0x2A2003E0u | (static_cast<uint32_t>(rm) << 16) | static_cast<uint32_t>(rd)); }
void JitCompiler::Asm::MvnX(int rd, int rm) { Emit(0xAA2003E0u | (static_cast<uint32_t>(rm) << 16) | static_cast<uint32_t>(rd)); }

void JitCompiler::Asm::AndW(int rd, int rn, int rm) { Emit(Enc3(0x0A000000u, rd, rn, rm)); }
void JitCompiler::Asm::OrrW(int rd, int rn, int rm) { Emit(Enc3(0x2A000000u, rd, rn, rm)); }
void JitCompiler::Asm::EorW(int rd, int rn, int rm) { Emit(Enc3(0x4A000000u, rd, rn, rm)); }
void JitCompiler::Asm::AndX(int rd, int rn, int rm) { Emit(Enc3(0x8A000000u, rd, rn, rm)); }
void JitCompiler::Asm::OrrX(int rd, int rn, int rm) { Emit(Enc3(0xAA000000u, rd, rn, rm)); }
void JitCompiler::Asm::EorX(int rd, int rn, int rm) { Emit(Enc3(0xCA000000u, rd, rn, rm)); }

void JitCompiler::Asm::LslW(int rd, int rn, int rm) { Emit(Enc3(0x1AC02000u, rd, rn, rm)); }
void JitCompiler::Asm::LsrW(int rd, int rn, int rm) { Emit(Enc3(0x1AC02400u, rd, rn, rm)); }
void JitCompiler::Asm::AsrW(int rd, int rn, int rm) { Emit(Enc3(0x1AC02800u, rd, rn, rm)); }
void JitCompiler::Asm::LslX(int rd, int rn, int rm) { Emit(Enc3(0x9AC02000u, rd, rn, rm)); }
void JitCompiler::Asm::LsrX(int rd, int rn, int rm) { Emit(Enc3(0x9AC02400u, rd, rn, rm)); }
void JitCompiler::Asm::AsrX(int rd, int rn, int rm) { Emit(Enc3(0x9AC02800u, rd, rn, rm)); }

void JitCompiler::Asm::CmpW(int rn, int rm) {
    // SUBS wzr, wN, wM
    Emit(Enc3(0x6B000000u, kZR, rn, rm));
}

void JitCompiler::Asm::CmpX(int rn, int rm) {
    Emit(Enc3(0xEB000000u, kZR, rn, rm));
}

void JitCompiler::Asm::CmpWImm(int rn, uint16_t imm) {
    // SUBS wzr, wN, #imm12
    Emit(0x7100001Fu | ((static_cast<uint32_t>(imm) & 0xFFFu) << 10) |
         (static_cast<uint32_t>(rn) << 5));
}

void JitCompiler::Asm::Sxtw(int rd, int rn) {
    // SBFM xD, xN, #0, #31
    Emit(0x93407C00u | (static_cast<uint32_t>(rn) << 5) | static_cast<uint32_t>(rd));
}

void JitCompiler::Asm::Cset(int rd, Cond cond) {
    // CSINC wD, wzr, wzr, invert(cond). The condition field holds the INVERSE, which is
    // why the low bit is flipped: cset wD, eq encodes cond=ne.
    const uint32_t inv = static_cast<uint32_t>(cond) ^ 1u;
    Emit(0x1A9F07E0u | (inv << 12) | static_cast<uint32_t>(rd));
}

size_t JitCompiler::Asm::BranchPlaceholder() {
    const size_t at = code.size();
    Emit(0x14000000u);  // b #0, patched later
    return at;
}

size_t JitCompiler::Asm::BranchCondPlaceholder(Cond cond) {
    const size_t at = code.size();
    Emit(0x54000000u | static_cast<uint32_t>(cond));
    return at;
}

void JitCompiler::Asm::PatchBranch(size_t at, size_t target) {
    if (at >= code.size()) return;
    // Branch offsets are in instructions, relative to the branch itself.
    const int64_t delta = static_cast<int64_t>(target) - static_cast<int64_t>(at);
    uint32_t inst = code[at];
    if ((inst & 0xFC000000u) == 0x14000000u) {
        // B: imm26
        code[at] = 0x14000000u | (static_cast<uint32_t>(delta) & 0x03FFFFFFu);
    } else {
        // B.cond: imm19 at bit 5
        code[at] = (inst & 0xFF00001Fu) |
                   ((static_cast<uint32_t>(delta) & 0x7FFFFu) << 5);
    }
}

void JitCompiler::Asm::Ret() { Emit(0xD65F03C0u); }

// Compiler

bool JitCompiler::IsAvailable() {
    // Compiled code runs only on arm64; encoder stays available for tests.
#if defined(__aarch64__) || defined(__arm64__)
    return JitCache::IsAvailable();
#else
    return false;
#endif
}
size_t JitCompiler::CompiledCount() { return g_compiled.load(); }
size_t JitCompiler::RefusedCount() { return g_refused.load(); }

void* JitCompiler::AllocateAndCommit(const std::vector<uint32_t>& code) {
    if (code.empty()) return nullptr;
    const size_t bytes = code.size() * sizeof(uint32_t);
    void* mem = JitCache::Instance().Allocate(bytes);
    if (mem == nullptr) {
        std::fprintf(stderr, "[KuART][JIT] allocation refused by executable-memory or pressure budget\n");
        return nullptr;
    }
    std::memcpy(mem, code.data(), bytes);
    if (!JitCache::Instance().Commit(mem, bytes)) return nullptr;
    return mem;
}

JitEntry JitCompiler::Compile(const DexMethod* method, std::string* reason) {
    const auto refuse = [&](const char* why) -> JitEntry {
        if (reason != nullptr) *reason = why;
        g_refused.fetch_add(1);
        return nullptr;
    };

    if (method == nullptr || method->code_item == nullptr || method->dex_file == nullptr) {
        return refuse("no code item");
    }
    if (!IsAvailable()) return refuse("no executable memory");

    art::CodeItemDataAccessor accessor(*method->dex_file, method->code_item);
    // Try blocks need exception paths; out of scope.
    if (accessor.TriesSize() != 0) return refuse("has try/catch");

    const uint16_t* insns = accessor.Insns();
    const uint32_t insns_size = accessor.InsnsSizeInCodeUnits();
    // Registers use a 12-bit scaled offset; refuse oversized methods.
    if (method->registers_size > 4000) return refuse("too many registers");

    Asm a;
    // dex_pc to emitted index for branch patching.
    std::vector<size_t> pcToIndex(insns_size, SIZE_MAX);
    struct PendingBranch {
        size_t at;          // index in a.code
        uint32_t targetPc;  // dex_pc to land on
    };
    std::vector<PendingBranch> pending;

    // Bail to the interpreter at pc.
    const auto emitBail = [&](uint32_t pc) {
        // *out_pc = pc; return -1
        a.MovzW(kTmpA, static_cast<uint16_t>(pc & 0xFFFF));
        if ((pc >> 16) != 0) a.MovkW(kTmpA, static_cast<uint16_t>(pc >> 16), 16);
        a.StrW(kTmpA, kOutPc, 0);
        a.MovnW(0, 0);  // w0 = -1
        a.Ret();
    };

    for (uint32_t dex_pc = 0; dex_pc < insns_size;) {
        pcToIndex[dex_pc] = a.code.size();
        const Instruction* inst = Instruction::At(insns + dex_pc);
        const uint32_t next_pc = dex_pc + static_cast<uint32_t>(inst->SizeInCodeUnits());

        switch (inst->Opcode()) {
            case Instruction::NOP:
                break;

            // Moves: one 64-bit slot copy covers every width.
            case Instruction::MOVE:
            case Instruction::MOVE_OBJECT:
            case Instruction::MOVE_WIDE:
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_12x()));
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            case Instruction::MOVE_FROM16:
            case Instruction::MOVE_OBJECT_FROM16:
            case Instruction::MOVE_WIDE_FROM16:
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_22x()));
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_22x()));
                break;
            case Instruction::MOVE_16:
            case Instruction::MOVE_OBJECT_16:
            case Instruction::MOVE_WIDE_16:
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_32x()));
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_32x()));
                break;

            // Constants.
            case Instruction::CONST_4: {
                // The 4-bit literal is signed; DexValue::Int zero-extends the upper
                // half, so the whole slot is written to keep it consistent with the
                // interpreter (which assigns a fresh DexValue).
                const int32_t v = inst->VRegB_11n();
                a.MovzX(kTmpA, static_cast<uint16_t>(static_cast<uint32_t>(v) & 0xFFFF));
                if ((static_cast<uint32_t>(v) >> 16) != 0) {
                    a.MovkX(kTmpA, static_cast<uint16_t>(static_cast<uint32_t>(v) >> 16), 16);
                }
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_11n()));
                break;
            }
            case Instruction::CONST_16: {
                const int32_t v = inst->VRegB_21s();
                a.MovzX(kTmpA, static_cast<uint16_t>(static_cast<uint32_t>(v) & 0xFFFF));
                if ((static_cast<uint32_t>(v) >> 16) != 0) {
                    a.MovkX(kTmpA, static_cast<uint16_t>(static_cast<uint32_t>(v) >> 16), 16);
                }
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_21s()));
                break;
            }
            case Instruction::CONST: {
                const uint32_t v = static_cast<uint32_t>(inst->VRegB_31i());
                a.MovzX(kTmpA, static_cast<uint16_t>(v & 0xFFFF));
                if ((v >> 16) != 0) a.MovkX(kTmpA, static_cast<uint16_t>(v >> 16), 16);
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_31i()));
                break;
            }

            // Returns.
            case Instruction::RETURN_VOID:
                // Nothing is written to *out_result: the caller ignores it for a void
                // method, exactly as the interpreter's default-constructed DexValue is
                // ignored.
                a.MovzW(0, 0);
                a.Ret();
                break;
            case Instruction::RETURN:
            case Instruction::RETURN_OBJECT:
            case Instruction::RETURN_WIDE:
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegA_11x()));
                a.StrX(kTmpA, kResult, 0);
                a.MovzW(0, 0);
                a.Ret();
                break;

            // Int arithmetic, two source registers.
            case Instruction::ADD_INT:
            case Instruction::SUB_INT:
            case Instruction::MUL_INT:
            case Instruction::AND_INT:
            case Instruction::OR_INT:
            case Instruction::XOR_INT:
            case Instruction::SHL_INT:
            case Instruction::SHR_INT:
            case Instruction::USHR_INT: {
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegB_23x()));
                a.LdrW(kTmpB, kRegs, VRegOffset(inst->VRegC_23x()));
                switch (inst->Opcode()) {
                    case Instruction::ADD_INT: a.AddW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SUB_INT: a.SubW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::MUL_INT: a.MulW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::AND_INT: a.AndW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::OR_INT:  a.OrrW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::XOR_INT: a.EorW(kTmpC, kTmpA, kTmpB); break;
                    // DEX masks the shift amount to 5 bits for int; arm64's variable
                    // shift already uses only the low 5 bits of the amount for the
                    // 32-bit form, so no explicit mask is needed.
                    case Instruction::SHL_INT: a.LslW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SHR_INT: a.AsrW(kTmpC, kTmpA, kTmpB); break;
                    default:                   a.LsrW(kTmpC, kTmpA, kTmpB); break;
                }
                // Store the full slot so the upper 32 bits are defined: an int vreg
                // later read as part of a wide pair would otherwise see stale bytes.
                a.MovzX(kTmpA, 0);
                a.OrrX(kTmpA, kTmpA, kTmpC);
                a.MovzX(kTmpB, 0xFFFF);
                a.MovkX(kTmpB, 0xFFFF, 16);
                a.AndX(kTmpA, kTmpA, kTmpB);
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_23x()));
                break;
            }

            // Long arithmetic.
            case Instruction::ADD_LONG:
            case Instruction::SUB_LONG:
            case Instruction::MUL_LONG:
            case Instruction::AND_LONG:
            case Instruction::OR_LONG:
            case Instruction::XOR_LONG: {
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_23x()));
                a.LdrX(kTmpB, kRegs, VRegOffset(inst->VRegC_23x()));
                switch (inst->Opcode()) {
                    case Instruction::ADD_LONG: a.AddX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SUB_LONG: a.SubX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::MUL_LONG: a.MulX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::AND_LONG: a.AndX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::OR_LONG:  a.OrrX(kTmpC, kTmpA, kTmpB); break;
                    default:                    a.EorX(kTmpC, kTmpA, kTmpB); break;
                }
                a.StrX(kTmpC, kRegs, VRegOffset(inst->VRegA_23x()));
                break;
            }

            // Unary.
            case Instruction::NEG_INT:
            case Instruction::NOT_INT: {
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegB_12x()));
                if (inst->Opcode() == Instruction::NEG_INT) a.NegW(kTmpB, kTmpA);
                else a.MvnW(kTmpB, kTmpA);
                a.MovzX(kTmpC, 0xFFFF);
                a.MovkX(kTmpC, 0xFFFF, 16);
                a.AndX(kTmpB, kTmpB, kTmpC);
                a.StrX(kTmpB, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            }
            case Instruction::NEG_LONG:
            case Instruction::NOT_LONG: {
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_12x()));
                if (inst->Opcode() == Instruction::NEG_LONG) a.NegX(kTmpB, kTmpA);
                else a.MvnX(kTmpB, kTmpA);
                a.StrX(kTmpB, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            }
            case Instruction::INT_TO_LONG: {
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegB_12x()));
                a.Sxtw(kTmpB, kTmpA);
                a.StrX(kTmpB, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            }
            case Instruction::LONG_TO_INT: {
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_12x()));
                a.MovzX(kTmpC, 0xFFFF);
                a.MovkX(kTmpC, 0xFFFF, 16);
                a.AndX(kTmpA, kTmpA, kTmpC);
                a.StrX(kTmpA, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            }

            // Int arithmetic, literal operand.
            case Instruction::ADD_INT_LIT8:
            case Instruction::MUL_INT_LIT8:
            case Instruction::AND_INT_LIT8:
            case Instruction::OR_INT_LIT8:
            case Instruction::XOR_INT_LIT8: {
                const int32_t lit = inst->VRegC_22b();
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegB_22b()));
                a.MovzW(kTmpB, static_cast<uint16_t>(static_cast<uint32_t>(lit) & 0xFFFF));
                if ((static_cast<uint32_t>(lit) >> 16) != 0) {
                    a.MovkW(kTmpB, static_cast<uint16_t>(static_cast<uint32_t>(lit) >> 16), 16);
                }
                switch (inst->Opcode()) {
                    case Instruction::ADD_INT_LIT8: a.AddW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::MUL_INT_LIT8: a.MulW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::AND_INT_LIT8: a.AndW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::OR_INT_LIT8:  a.OrrW(kTmpC, kTmpA, kTmpB); break;
                    default:                        a.EorW(kTmpC, kTmpA, kTmpB); break;
                }
                a.MovzX(kTmpA, 0xFFFF);
                a.MovkX(kTmpA, 0xFFFF, 16);
                a.AndX(kTmpC, kTmpC, kTmpA);
                a.StrX(kTmpC, kRegs, VRegOffset(inst->VRegA_22b()));
                break;
            }

            // /2addr forms: destination is also the first source.
            case Instruction::ADD_INT_2ADDR:
            case Instruction::SUB_INT_2ADDR:
            case Instruction::MUL_INT_2ADDR:
            case Instruction::AND_INT_2ADDR:
            case Instruction::OR_INT_2ADDR:
            case Instruction::XOR_INT_2ADDR:
            case Instruction::SHL_INT_2ADDR:
            case Instruction::SHR_INT_2ADDR:
            case Instruction::USHR_INT_2ADDR: {
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegA_12x()));
                a.LdrW(kTmpB, kRegs, VRegOffset(inst->VRegB_12x()));
                switch (inst->Opcode()) {
                    case Instruction::ADD_INT_2ADDR: a.AddW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SUB_INT_2ADDR: a.SubW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::MUL_INT_2ADDR: a.MulW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::AND_INT_2ADDR: a.AndW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::OR_INT_2ADDR:  a.OrrW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::XOR_INT_2ADDR: a.EorW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SHL_INT_2ADDR: a.LslW(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SHR_INT_2ADDR: a.AsrW(kTmpC, kTmpA, kTmpB); break;
                    default:                         a.LsrW(kTmpC, kTmpA, kTmpB); break;
                }
                a.MovzX(kTmpA, 0xFFFF);
                a.MovkX(kTmpA, 0xFFFF, 16);
                a.AndX(kTmpC, kTmpC, kTmpA);
                a.StrX(kTmpC, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            }
            case Instruction::ADD_LONG_2ADDR:
            case Instruction::SUB_LONG_2ADDR:
            case Instruction::MUL_LONG_2ADDR:
            case Instruction::AND_LONG_2ADDR:
            case Instruction::OR_LONG_2ADDR:
            case Instruction::XOR_LONG_2ADDR: {
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegA_12x()));
                a.LdrX(kTmpB, kRegs, VRegOffset(inst->VRegB_12x()));
                switch (inst->Opcode()) {
                    case Instruction::ADD_LONG_2ADDR: a.AddX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::SUB_LONG_2ADDR: a.SubX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::MUL_LONG_2ADDR: a.MulX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::AND_LONG_2ADDR: a.AndX(kTmpC, kTmpA, kTmpB); break;
                    case Instruction::OR_LONG_2ADDR:  a.OrrX(kTmpC, kTmpA, kTmpB); break;
                    default:                          a.EorX(kTmpC, kTmpA, kTmpB); break;
                }
                a.StrX(kTmpC, kRegs, VRegOffset(inst->VRegA_12x()));
                break;
            }

            // Comparisons producing -1/0/1.
            case Instruction::CMP_LONG: {
                // (b > c) - (b < c), via two csets. Branchless, and the only shape that
                // gives all three results without a jump.
                a.LdrX(kTmpA, kRegs, VRegOffset(inst->VRegB_23x()));
                a.LdrX(kTmpB, kRegs, VRegOffset(inst->VRegC_23x()));
                a.CmpX(kTmpA, kTmpB);
                a.Cset(kTmpC, Asm::kGT);
                a.Cset(kTmpA, Asm::kLT);
                a.SubW(kTmpC, kTmpC, kTmpA);
                a.MovzX(kTmpA, 0xFFFF);
                a.MovkX(kTmpA, 0xFFFF, 16);
                a.AndX(kTmpC, kTmpC, kTmpA);
                a.StrX(kTmpC, kRegs, VRegOffset(inst->VRegA_23x()));
                break;
            }

            // Unconditional branches.
            case Instruction::GOTO:
            case Instruction::GOTO_16:
            case Instruction::GOTO_32: {
                int32_t offset = 0;
                if (inst->Opcode() == Instruction::GOTO) offset = inst->VRegA_10t();
                else if (inst->Opcode() == Instruction::GOTO_16) offset = inst->VRegA_20t();
                else offset = inst->VRegA_30t();
                const int64_t target = static_cast<int64_t>(dex_pc) + offset;
                if (target < 0 || target >= insns_size) return refuse("goto out of range");
                pending.push_back({a.BranchPlaceholder(), static_cast<uint32_t>(target)});
                break;
            }

            // Conditional branches, two registers.
            case Instruction::IF_EQ:
            case Instruction::IF_NE:
            case Instruction::IF_LT:
            case Instruction::IF_GE:
            case Instruction::IF_GT:
            case Instruction::IF_LE: {
                const int64_t target =
                    static_cast<int64_t>(dex_pc) + inst->VRegC_22t();
                if (target < 0 || target >= insns_size) return refuse("if out of range");
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegA_22t()));
                a.LdrW(kTmpB, kRegs, VRegOffset(inst->VRegB_22t()));
                a.CmpW(kTmpA, kTmpB);
                Asm::Cond cond = Asm::kEQ;
                switch (inst->Opcode()) {
                    case Instruction::IF_EQ: cond = Asm::kEQ; break;
                    case Instruction::IF_NE: cond = Asm::kNE; break;
                    case Instruction::IF_LT: cond = Asm::kLT; break;
                    case Instruction::IF_GE: cond = Asm::kGE; break;
                    case Instruction::IF_GT: cond = Asm::kGT; break;
                    default:                 cond = Asm::kLE; break;
                }
                pending.push_back({a.BranchCondPlaceholder(cond),
                                   static_cast<uint32_t>(target)});
                break;
            }

            // Conditional branches against zero.
            case Instruction::IF_EQZ:
            case Instruction::IF_NEZ:
            case Instruction::IF_LTZ:
            case Instruction::IF_GEZ:
            case Instruction::IF_GTZ:
            case Instruction::IF_LEZ: {
                const int64_t target =
                    static_cast<int64_t>(dex_pc) + inst->VRegB_21t();
                if (target < 0 || target >= insns_size) return refuse("ifz out of range");
                a.LdrW(kTmpA, kRegs, VRegOffset(inst->VRegA_21t()));
                a.CmpWImm(kTmpA, 0);
                Asm::Cond cond = Asm::kEQ;
                switch (inst->Opcode()) {
                    case Instruction::IF_EQZ: cond = Asm::kEQ; break;
                    case Instruction::IF_NEZ: cond = Asm::kNE; break;
                    case Instruction::IF_LTZ: cond = Asm::kLT; break;
                    case Instruction::IF_GEZ: cond = Asm::kGE; break;
                    case Instruction::IF_GTZ: cond = Asm::kGT; break;
                    default:                  cond = Asm::kLE; break;
                }
                pending.push_back({a.BranchCondPlaceholder(cond),
                                   static_cast<uint32_t>(target)});
                break;
            }

            default:
                // Uncompiled opcodes refuse the method; partial compile gains nothing.
                return refuse(Instruction::Name(inst->Opcode()));
        }

        dex_pc = next_pc;
    }

    // Fall off the end: the verifier guarantees this is unreachable, but emitting a bail
    // means an unreachable path cannot run into whatever code follows in the block.
    emitBail(insns_size);

    for (const PendingBranch& b : pending) {
        if (b.targetPc >= pcToIndex.size() || pcToIndex[b.targetPc] == SIZE_MAX) {
            // A branch into the middle of an instruction, or to an unemitted pc. Both
            // mean this method's control flow was not fully modelled, so nothing is
            // safe to run.
            return refuse("branch target not an instruction boundary");
        }
        a.PatchBranch(b.at, pcToIndex[b.targetPc]);
    }

    void* code = AllocateAndCommit(a.code);
    if (code == nullptr) return refuse("jit cache full");
    g_compiled.fetch_add(1);
    if (reason != nullptr) reason->clear();
    return reinterpret_cast<JitEntry>(code);
}

}  // namespace kuart
}  // namespace kudroid
