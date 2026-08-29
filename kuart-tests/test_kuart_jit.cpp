// Probe: the JIT, the executable-memory cache, and the OAT profile.
//
// The instruction encoder is the part that most needs a test and least shows its
// failures at runtime. A wrong bit does not crash — it produces a DIFFERENT valid
// instruction, so the compiled method returns a wrong answer that looks like an
// interpreter bug. Every encoding here is pinned against bytes taken from GNU as
// assembling the same mnemonic, so an edit that changes an encoding fails here instead
// of silently changing arithmetic.
//
// The cache and the profile are tested for the no-JIT path above all: on iOS without
// a debugger, and under LiveContainer's JITLess mode, executable memory is refused.
// That is the common case, not the exceptional one, and it must degrade to the
// interpreter rather than fail.
#include "kudroid/kuart/JitCache.h"
#include "kudroid/kuart/JitCompiler.h"
#include "kudroid/kuart/OatFile.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>  // sysconf(_SC_PAGESIZE), for the page-sharing check below

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::kuart::JitCache;
using kudroid::kuart::JitCompiler;
using kudroid::kuart::OatFile;
using Asm = JitCompiler::Asm;

void CheckEnc(const char* what, uint32_t got, uint32_t want) {
    if (got == want) {
        std::printf("  OK   %-24s 0x%08X\n", what, got);
    } else {
        std::printf("  FAIL %-24s got 0x%08X, want 0x%08X\n", what, got, want);
        ++g_failures;
    }
}

// One encoding per call, so a failure names exactly which one.
#define ENC(expr, want)                     \
    do {                                    \
        Asm a;                              \
        a.expr;                             \
        if (a.code.size() != 1) {           \
            std::printf("  FAIL %s emitted %zu instructions\n", #expr, a.code.size()); \
            ++g_failures;                   \
        } else {                            \
            CheckEnc(#expr, a.code[0], want); \
        }                                   \
    } while (0)

void TestEncodings() {
    std::printf("-- encoder vs GNU as --\n");

    // Loads/stores. The immediate is SCALED — by 8 for the 64-bit form and 4 for the
    // 32-bit one — which is the easiest thing to get wrong and the hardest to notice,
    // because a wrong scale still addresses valid memory: the wrong register.
    ENC(LdrX(9, 0, 8), 0xF9400409u);
    ENC(StrX(9, 0, 16), 0xF9000809u);
    ENC(LdrW(9, 0, 4), 0xB9400409u);
    ENC(StrW(9, 0, 8), 0xB9000809u);

    ENC(MovzW(9, 5), 0x528000A9u);
    ENC(MovnW(9, 0), 0x12800009u);
    ENC(MovkW(9, 0x1234, 16), 0x72A24689u);
    ENC(MovzX(9, 5), 0xD28000A9u);
    ENC(MovkX(9, 0xabcd, 32), 0xF2D579A9u);
    ENC(MovX(9, 10), 0xAA0A03E9u);

    ENC(AddW(11, 9, 10), 0x0B0A012Bu);
    ENC(SubW(11, 9, 10), 0x4B0A012Bu);
    ENC(MulW(11, 9, 10), 0x1B0A7D2Bu);
    ENC(SdivW(11, 9, 10), 0x1ACA0D2Bu);
    ENC(MsubW(11, 9, 10, 11), 0x1B0AAD2Bu);
    ENC(AddX(11, 9, 10), 0x8B0A012Bu);
    ENC(SubX(11, 9, 10), 0xCB0A012Bu);
    ENC(MulX(11, 9, 10), 0x9B0A7D2Bu);

    // neg/mvn are aliases: neg is SUB from zr, mvn is ORN with zr. Encoding them as
    // the alias rather than the underlying form is where the register fields move.
    ENC(NegW(11, 10), 0x4B0A03EBu);
    ENC(NegX(11, 10), 0xCB0A03EBu);
    ENC(MvnW(11, 10), 0x2A2A03EBu);
    ENC(MvnX(11, 10), 0xAA2A03EBu);

    ENC(AndW(11, 9, 10), 0x0A0A012Bu);
    ENC(OrrW(11, 9, 10), 0x2A0A012Bu);
    ENC(EorW(11, 9, 10), 0x4A0A012Bu);
    ENC(LslW(11, 9, 10), 0x1ACA212Bu);
    ENC(LsrW(11, 9, 10), 0x1ACA252Bu);
    ENC(AsrW(11, 9, 10), 0x1ACA292Bu);
    ENC(AndX(11, 9, 10), 0x8A0A012Bu);
    ENC(OrrX(11, 9, 10), 0xAA0A012Bu);
    ENC(EorX(11, 9, 10), 0xCA0A012Bu);
    ENC(LslX(11, 9, 10), 0x9ACA212Bu);
    ENC(LsrX(11, 9, 10), 0x9ACA252Bu);
    ENC(AsrX(11, 9, 10), 0x9ACA292Bu);

    ENC(CmpW(9, 10), 0x6B0A013Fu);
    ENC(CmpX(9, 10), 0xEB0A013Fu);
    ENC(CmpWImm(9, 0), 0x7100013Fu);
    ENC(Sxtw(11, 9), 0x93407D2Bu);

    // cset holds the INVERSE condition, because it is CSINC with zr. Getting this
    // wrong inverts every comparison the JIT compiles — arithmetic stays right and
    // control flow goes backwards, which is why each of the six is pinned.
    ENC(Cset(11, Asm::kEQ), 0x1A9F17EBu);
    ENC(Cset(11, Asm::kNE), 0x1A9F07EBu);
    ENC(Cset(11, Asm::kLT), 0x1A9FA7EBu);
    ENC(Cset(11, Asm::kGE), 0x1A9FB7EBu);
    ENC(Cset(11, Asm::kGT), 0x1A9FD7EBu);
    ENC(Cset(11, Asm::kLE), 0x1A9FC7EBu);

    ENC(Ret(), 0xD65F03C0u);
}

void TestBranchPatching() {
    std::printf("-- branch patching --\n");

    // Forward unconditional: b +2 instructions.
    {
        Asm a;
        const size_t at = a.BranchPlaceholder();
        a.Ret();
        a.Ret();
        a.PatchBranch(at, 2);
        // b #8 encodes imm26 = 2.
        CheckEnc("b forward 2", a.code[at], 0x14000002u);
    }

    // Backward unconditional: the offset is negative and must sign-extend within
    // imm26. A missing mask here produces a branch to a wildly wrong address.
    {
        Asm a;
        a.Ret();
        a.Ret();
        const size_t at = a.BranchPlaceholder();
        a.PatchBranch(at, 0);
        // b -8 => imm26 = -2 = 0x3FFFFFE.
        CheckEnc("b backward 2", a.code[at], 0x17FFFFFEu);
    }

    // Conditional forward: imm19 sits at bit 5 and the condition must survive the
    // patch. Overwriting the whole word would drop the condition and turn every
    // conditional branch into b.eq.
    {
        Asm a;
        const size_t at = a.BranchCondPlaceholder(Asm::kLT);
        a.Ret();
        a.Ret();
        a.PatchBranch(at, 2);
        CheckEnc("b.lt forward 2", a.code[at], 0x5400004Bu);
    }

    // Conditional backward.
    {
        Asm a;
        a.Ret();
        const size_t at = a.BranchCondPlaceholder(Asm::kNE);
        a.PatchBranch(at, 0);
        // b.ne -4 => imm19 = -1.
        CheckEnc("b.ne backward 1", a.code[at], 0x54FFFFE1u);
    }
}

void TestJitCache() {
    std::printf("-- jit cache --\n");

    JitCache& cache = JitCache::Instance();
    const bool available = JitCache::IsAvailable();
    std::printf("       executable memory: %s\n", available ? "available" : "REFUSED");

    // The same answer every time. A JIT that re-probes could compile a method and then
    // find the memory unavailable at commit, with no way back for that method.
    Check(JitCache::IsAvailable() == available, "IsAvailable is stable across calls");

    // The COMPILER is only available on arm64, whatever the cache says: the encoder
    // emits AArch64 and those bytes are meaningless to any other decoder. On a host
    // build the cache can hand out executable memory and the compiler must still
    // decline, or the first compiled method faults on its first instruction.
#if defined(__aarch64__) || defined(__arm64__)
    Check(JitCompiler::IsAvailable() == available,
          "on arm64 the compiler follows the cache");
#else
    Check(!JitCompiler::IsAvailable(),
          "off arm64 the compiler declines even when executable memory exists");
#endif

    void* p = cache.Allocate(64);
    if (available) {
        Check(p != nullptr, "Allocate returns memory when JIT is available");
        if (p != nullptr) {
            Check((reinterpret_cast<uintptr_t>(p) & 3u) == 0,
                  "allocation is 4-byte aligned for arm64 instructions");
            // Write a ret and commit, which is the smallest thing that proves the
            // memory really became executable rather than merely being reported so.
            const uint32_t ret = 0xD65F03C0u;
            std::memcpy(p, &ret, sizeof(ret));
            Check(cache.Commit(p, sizeof(ret)), "Commit makes the code executable");
        }

        // Two methods in a row: the second must be writable AFTER the first is
        // committed. This is a regression test for a crash on device, where Minecraft
        // faulted with SIGBUS inside memcpy while filling the second compiled method.
        //
        // Commit() changes protection a page at a time, so committing a method makes
        // all of its pages read-only. Allocation used to be 4-byte aligned bump
        // allocation, which put the next method in the same page — memory that was
        // already RX by the time anything was written to it.
        //
        // Nothing here is arm64-specific: JitCache only maps and protects memory, so
        // this reproduces on any host where mprotect works, which is where it should
        // have been caught before shipping.
        void* first = cache.Allocate(16);
        if (first != nullptr) {
            const uint32_t ret = 0xD65F03C0u;
            std::memcpy(first, &ret, sizeof(ret));
            Check(cache.Commit(first, sizeof(ret)), "first method commits");

            void* second = cache.Allocate(16);
            Check(second != nullptr, "a second method can be allocated after a commit");
            if (second != nullptr) {
                const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
                const uintptr_t a = reinterpret_cast<uintptr_t>(first);
                const uintptr_t b = reinterpret_cast<uintptr_t>(second);
                Check((a & (page - 1)) == 0, "an allocation starts on a page boundary");
                Check((a / page) != (b / page),
                      "two allocations never share a page (Commit would seal the second)");
                // The write that faulted on device. Reaching the Check below at all is
                // the result being tested.
                std::memcpy(second, &ret, sizeof(ret));
                Check(cache.Commit(second, sizeof(ret)),
                      "second method is still writable, then commits");
            }
        }

        // A request larger than a block cannot be satisfied by bump allocation and
        // must be refused rather than silently truncated.
        Check(cache.Allocate(JitCache::kMaxTotalBytes * 2) == nullptr,
              "an oversized request is refused, not truncated");
    } else {
        // The path that matters on a stock iOS install and under LiveContainer JITLess.
        Check(p == nullptr, "Allocate returns null when JIT is unavailable");
        Check(!cache.Commit(nullptr, 0), "Commit(null) is safe");
        Check(!JitCompiler::IsAvailable(), "the compiler reports itself unavailable too");
    }

    Check(cache.Allocate(0) == nullptr, "a zero-size request is refused");
    Check(!cache.Commit(nullptr, 16), "Commit rejects a null pointer");
}

void TestCompilerRefusal() {
    std::printf("-- compiler refusal --\n");

    // A null method must be refused rather than dereferenced. This is the guard that
    // makes it safe to call Compile on anything the interpreter holds.
    std::string reason;
    Check(JitCompiler::Compile(nullptr, &reason) == nullptr,
          "Compile(nullptr) is refused, not a crash");
    Check(!reason.empty(), std::string("the refusal says why: ") + reason);

    // Refusal is counted, so a run can be inspected for which opcodes are worth
    // implementing next.
    const size_t before = JitCompiler::RefusedCount();
    (void)JitCompiler::Compile(nullptr, nullptr);
    Check(JitCompiler::RefusedCount() > before, "refusals are counted");
    // reason may be null: a caller that does not care must not have to supply one.
    Check(true, "Compile accepts a null reason pointer");
}

void TestOatFile() {
    std::printf("-- oat profile --\n");

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / "kudroid_oat_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string path = (dir / "app.oat").string();

    // Absent file: not an error, just no profile. Reporting failure here would make
    // every first run look broken.
    {
        OatFile oat;
        Check(!oat.Load(path), "loading a missing profile reports absence");
        Check(oat.empty(), "and leaves the profile empty");
    }

    // Round-trip. The point of the file is that the next run starts knowing what the
    // last one learned, so this is the property that gives it any value at all.
    {
        OatFile oat;
        oat.Note(0xAAAA1111u, 42, OatFile::MethodState::kHot);
        oat.Note(0xAAAA1111u, 99, OatFile::MethodState::kRefused);
        oat.Note(0xBBBB2222u, 7, OatFile::MethodState::kHot);
        Check(oat.size() == 3, "three methods recorded");
        Check(oat.Save(path), "profile saved");
    }
    {
        OatFile oat;
        Check(oat.Load(path), "profile loaded back");
        Check(oat.size() == 3, "all three entries survived the round trip");
        Check(oat.Lookup(0xAAAA1111u, 42) == OatFile::MethodState::kHot,
              "a hot method is still hot");
        Check(oat.Lookup(0xAAAA1111u, 99) == OatFile::MethodState::kRefused,
              "a refused method is still refused, so it is not retried");
        Check(oat.Lookup(0xBBBB2222u, 7) == OatFile::MethodState::kHot,
              "a method from another DEX is kept separately");
        // The same index in a different DEX is a different method. Keying on the index
        // alone would apply one DEX's profile to another's bytecode.
        Check(oat.Lookup(0xBBBB2222u, 42) == OatFile::MethodState::kUnknown,
              "the same index in another DEX is not confused with it");
        Check(oat.Lookup(0xAAAA1111u, 1234) == OatFile::MethodState::kUnknown,
              "an unrecorded method is unknown");
    }

    // A DEX whose checksum changed means the app was updated: every method index in
    // those entries now refers to different bytecode, so they must go. Keeping them
    // would compile whatever method happens to sit at that index now.
    {
        OatFile oat;
        Check(oat.Load(path), "profile loaded for invalidation");
        oat.InvalidateChecksum(0xAAAA1111u);
        Check(oat.Lookup(0xAAAA1111u, 42) == OatFile::MethodState::kUnknown,
              "entries for the changed DEX are dropped");
        Check(oat.Lookup(0xBBBB2222u, 7) == OatFile::MethodState::kHot,
              "entries for an unchanged DEX are kept");
    }

    // A file from a different build must be rejected, not misread as records.
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f != nullptr) {
            const uint32_t header[4] = {0x4B554F41u, 999u, 0u, 0u};
            std::fwrite(header, sizeof(header), 1, f);
            std::fclose(f);
        }
        OatFile oat;
        Check(!oat.Load(path), "a profile from another version is rejected");
        Check(oat.empty(), "and nothing is carried over from it");
    }

    // Garbage in the magic field likewise. This is the case where misreading would be
    // worst: arbitrary bytes interpreted as an entry count.
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f != nullptr) {
            const char junk[32] = "not an oat file at all";
            std::fwrite(junk, sizeof(junk), 1, f);
            std::fclose(f);
        }
        OatFile oat;
        Check(!oat.Load(path), "a file that is not a profile is rejected");
    }

    // Truncated: keep what was readable. Discarding a partial profile throws away
    // work for nothing, since the entries that did load are still correct.
    {
        OatFile full;
        full.Note(0xCCCC3333u, 1, OatFile::MethodState::kHot);
        full.Note(0xCCCC3333u, 2, OatFile::MethodState::kHot);
        full.Save(path);
        // Chop the last record in half.
        const auto size = fs::file_size(path, ec);
        if (!ec && size > 6) fs::resize_file(path, size - 6, ec);

        OatFile oat;
        Check(oat.Load(path), "a truncated profile still loads");
        Check(oat.size() == 1, "the complete records are kept and the partial one dropped");
    }

    // kUnknown is not a state worth storing: it means "no information", and writing it
    // would fill the file with entries that say nothing.
    {
        OatFile oat;
        oat.Note(0xDDDD4444u, 5, OatFile::MethodState::kUnknown);
        Check(oat.empty(), "recording kUnknown stores nothing");
    }

    fs::remove_all(dir, ec);
}

}  // namespace

int main() {
    std::printf("=== KuART JIT + OAT ===\n");
    TestEncodings();
    TestBranchPatching();
    TestJitCache();
    TestCompilerRefusal();
    TestOatFile();

    if (g_failures == 0) {
        std::printf("=== KuART JIT test PASSED ===\n");
        return 0;
    }
    std::printf("=== KuART JIT test FAILED (%d) ===\n", g_failures);
    return 1;
}
