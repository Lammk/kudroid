// Dump JitCompiler output as a C header; scripts/test-jit-arm64.sh runs it under qemu.
// Hand-written sequences keep the linker/loader out, so failures point at the encoder.
#include "kudroid/kuart/JitCompiler.h"

#include <cstdio>

using kudroid::kuart::JitCompiler;
using Asm = JitCompiler::Asm;

namespace {

// Byte offset of DEX virtual register `v` in the frame array, matching
// JitCompiler::VRegOffset.
constexpr unsigned VReg(unsigned v) { return v * 8u; }

// x0 = registers, x1 = out_result, x2 = out_pc. Scratch is x9/x10/x11.
constexpr int kRegs = 0, kResult = 1, kA = 9, kB = 10, kC = 11;

void Dump(const char* name, const Asm& a) {
    std::printf("static unsigned int %s[] = {", name);
    for (uint32_t w : a.code) std::printf("0x%08Xu,", w);
    std::printf("};\nstatic int %s_n = %zu;\n", name, a.code.size());
}

// Narrow a 32-bit result to the low half of the slot, which is what the compiler does
// after every int operation so an int vreg never carries stale upper bytes.
void ClampToInt(Asm& a, int reg) {
    a.MovzX(kA, 0xFFFF);
    a.MovkX(kA, 0xFFFF, 16);
    a.AndX(reg, reg, kA);
}

// return vN
void ReturnVReg(Asm& a, unsigned vreg) {
    a.LdrX(kA, kRegs, VReg(vreg));
    a.StrX(kA, kResult, 0);
    a.MovzW(0, 0);
    a.Ret();
}

}  // namespace

int main() {
    // ADD_INT v2, v0, v1 ; RETURN v2
    {
        Asm a;
        a.LdrW(kA, kRegs, VReg(0));
        a.LdrW(kB, kRegs, VReg(1));
        a.AddW(kC, kA, kB);
        ClampToInt(a, kC);
        a.StrX(kC, kRegs, VReg(2));
        ReturnVReg(a, 2);
        Dump("code_add", a);
    }

    // MUL_LONG v2, v0, v1 ; RETURN_WIDE v2
    {
        Asm a;
        a.LdrX(kA, kRegs, VReg(0));
        a.LdrX(kB, kRegs, VReg(1));
        a.MulX(kC, kA, kB);
        a.StrX(kC, kRegs, VReg(2));
        ReturnVReg(a, 2);
        Dump("code_mull", a);
    }

    // CMP_LONG v2, v0, v1 ; RETURN v2
    //
    // (a > b) - (a < b), branchless. This is the sequence most sensitive to the cset
    // encoding: cset carries the INVERSE condition, so a wrong bit here returns the
    // negation of every comparison, and arithmetic elsewhere stays correct.
    {
        Asm a;
        a.LdrX(kA, kRegs, VReg(0));
        a.LdrX(kB, kRegs, VReg(1));
        a.CmpX(kA, kB);
        a.Cset(kC, Asm::kGT);
        a.Cset(kA, Asm::kLT);
        a.SubW(kC, kC, kA);
        ClampToInt(a, kC);
        a.StrX(kC, kRegs, VReg(2));
        ReturnVReg(a, 2);
        Dump("code_cmpl", a);
    }

    // for (i = 1; i <= v1; ++i) acc += i;  return acc
    //
    // Exercises branch patching in both directions: a forward conditional exit whose
    // target is not yet emitted, and a backward unconditional jump to the loop top.
    // A sign or mask error in PatchBranch shows up here as a hang or a wrong sum.
    {
        Asm a;
        a.MovzX(kA, 0);
        a.StrX(kA, kRegs, VReg(2));  // acc = 0
        a.MovzX(kA, 1);
        a.StrX(kA, kRegs, VReg(3));  // i = 1

        const size_t top = a.code.size();
        a.LdrW(kA, kRegs, VReg(3));
        a.LdrW(kB, kRegs, VReg(1));
        a.CmpW(kA, kB);
        const size_t exitBranch = a.BranchCondPlaceholder(Asm::kGT);

        a.LdrW(kA, kRegs, VReg(2));
        a.LdrW(kB, kRegs, VReg(3));
        a.AddW(kC, kA, kB);
        ClampToInt(a, kC);
        a.StrX(kC, kRegs, VReg(2));

        a.LdrW(kA, kRegs, VReg(3));
        a.MovzW(kB, 1);
        a.AddW(kC, kA, kB);
        ClampToInt(a, kC);
        a.StrX(kC, kRegs, VReg(3));

        const size_t backBranch = a.BranchPlaceholder();
        const size_t exitTarget = a.code.size();
        ReturnVReg(a, 2);

        a.PatchBranch(backBranch, top);
        a.PatchBranch(exitBranch, exitTarget);
        Dump("code_loop", a);
    }

    return 0;
}
