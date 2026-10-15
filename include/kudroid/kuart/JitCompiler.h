// Baseline JIT for KuART: compiles a DEX method to arm64 machine code.
//
// Scope is deliberately narrow. This compiles the opcodes that dominate hot arithmetic
// and control flow — moves, constants, int/long arithmetic, comparisons, branches — and
// REFUSES everything else. A method containing one unsupported opcode is not compiled at
// all; it keeps running in the interpreter, which is already correct. That refusal is the
// design: a JIT that half-handles an opcode produces wrong answers silently, whereas one
// that declines produces the interpreter's answer slowly.
//
// Notably NOT compiled, and why:
//   - invoke-*      needs the full call protocol (arg marshalling, exception unwind,
//                   vtable dispatch, <clinit> triggering). The interpreter's version is
//                   several hundred lines and none of it is hot arithmetic.
//   - field/array   needs null checks that throw real Java exceptions, and bounds
//                   checks that must name the index.
//   - new-*, throw  needs the allocator and the exception machinery.
//   - float/double  needs the FP register file and NaN-exact comparison semantics.
// Each is a candidate for later; none is required to make a hot loop fast.
//
// Register model: DEX virtual registers stay in the frame's DexValue array in memory,
// addressed off x0. Values are loaded, operated on, and stored back per instruction.
// That is slower than allocating vregs to machine registers, but it keeps the frame
// authoritative at every instruction boundary — which is what lets an uncompiled path
// bail back to the interpreter mid-method, and what lets a stack trace read the frame.
#ifndef KUDROID_KUART_JITCOMPILER_H
#define KUDROID_KUART_JITCOMPILER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexValue.h"

namespace kudroid {
namespace kuart {

class DexFrame;

// Signature of compiled code.
//
// `registers` points at the frame's DexValue array; `out_result` receives the value a
// return instruction produced. The return value says how the method left:
//   >= 0  a return executed; the dex_pc is irrelevant and *out_result is the value
//   -1    an unsupported instruction was reached; resume the interpreter at *out_pc
using JitEntry = int32_t (*)(DexValue* registers, DexValue* out_result, uint32_t* out_pc);

class JitCompiler {
public:
    // Compile `method`, or return nullptr when it cannot be compiled.
    //
    // nullptr is the normal answer for most methods and is not an error. `reason`, when
    // non-null, receives what stopped the compile — useful for deciding what to
    // implement next, and for a test to assert that a specific opcode is refused rather
    // than silently miscompiled.
    static JitEntry Compile(const DexMethod* method, std::string* reason);

    // Whether the compiler can run at all (executable memory is available).
    static bool IsAvailable();

    // Number of methods compiled and refused this process, for diagnostics.
    static size_t CompiledCount();
    static size_t RefusedCount();

    // Everything below is the instruction encoder, exposed so a host test can check the
    // exact bytes against a real assembler. Encoding errors are invisible at runtime —
    // a wrong bit produces a valid but different instruction — so golden-byte tests are
    // the only way to know these are right.
    //
    // Register numbering is the arm64 one (0-30, 31 = zr/sp).
    struct Asm {
        std::vector<uint32_t> code;

        void Emit(uint32_t inst) { code.push_back(inst); }

        // ── loads/stores, unsigned scaled offset ──
        void LdrX(int rt, int rn, uint32_t byteOffset);   // ldr xT, [xN, #off]
        void StrX(int rt, int rn, uint32_t byteOffset);   // str xT, [xN, #off]
        void LdrW(int rt, int rn, uint32_t byteOffset);   // ldr wT, [xN, #off]
        void StrW(int rt, int rn, uint32_t byteOffset);   // str wT, [xN, #off]

        // ── moves ──
        void MovzW(int rd, uint16_t imm);                 // movz wD, #imm
        void MovnW(int rd, uint16_t imm);                 // movn wD, #imm
        void MovkW(int rd, uint16_t imm, int shift);      // movk wD, #imm, lsl #shift
        void MovzX(int rd, uint16_t imm);
        void MovkX(int rd, uint16_t imm, int shift);
        void MovX(int rd, int rm);                        // mov xD, xM  (orr xD, xzr, xM)

        // ── arithmetic, register form ──
        void AddW(int rd, int rn, int rm);
        void SubW(int rd, int rn, int rm);
        void MulW(int rd, int rn, int rm);
        void SdivW(int rd, int rn, int rm);
        void MsubW(int rd, int rn, int rm, int ra);       // rd = ra - rn*rm
        void AddX(int rd, int rn, int rm);
        void SubX(int rd, int rn, int rm);
        void MulX(int rd, int rn, int rm);
        void SdivX(int rd, int rn, int rm);
        void MsubX(int rd, int rn, int rm, int ra);
        void NegW(int rd, int rm);
        void NegX(int rd, int rm);
        void MvnW(int rd, int rm);
        void MvnX(int rd, int rm);

        // ── logical and shifts, register form ──
        void AndW(int rd, int rn, int rm);
        void OrrW(int rd, int rn, int rm);
        void EorW(int rd, int rn, int rm);
        void LslW(int rd, int rn, int rm);
        void LsrW(int rd, int rn, int rm);
        void AsrW(int rd, int rn, int rm);
        void AndX(int rd, int rn, int rm);
        void OrrX(int rd, int rn, int rm);
        void EorX(int rd, int rn, int rm);
        void LslX(int rd, int rn, int rm);
        void LsrX(int rd, int rn, int rm);
        void AsrX(int rd, int rn, int rm);

        // ── compare, branch, select ──
        void CmpW(int rn, int rm);
        void CmpX(int rn, int rm);
        void CmpWImm(int rn, uint16_t imm);
        void Sxtw(int rd, int rn);                        // sxtw xD, wN

        enum Cond {
            kEQ = 0x0, kNE = 0x1, kLT = 0xB, kGE = 0xA, kGT = 0xC, kLE = 0xD,
        };
        void Cset(int rd, Cond cond);                     // cset wD, cond
        // Branch with a placeholder offset; returns the index in `code` to patch.
        size_t BranchPlaceholder();
        size_t BranchCondPlaceholder(Cond cond);
        // Patch a branch emitted at `at` to land on instruction index `target`.
        void PatchBranch(size_t at, size_t target);

        void Ret();
    };

private:
    static void* AllocateAndCommit(const std::vector<uint32_t>& code);
};

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_JITCOMPILER_H
