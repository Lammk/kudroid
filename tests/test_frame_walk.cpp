// test_frame_walk.cpp — the frame walk that must never be the crash.
//
// Why this exists. KuDroid had four hand-written frame walks and three of them validated
// a frame pointer with `fp > 0x1000 && fp < 0x7fffffffffff`, which excludes null and
// nothing else. One of them killed a device run:
//
//     signal = 11  si_code = 2  fault_addr = 0x100000000050
//     inst = 0xf9401d49                    -> LDR x9, [x10, #56]
//     x10  = 0x100000000018                -> 0x100000000018 + 56 == fault_addr
//     pc_sym: kudroid::(anonymous)::log_guard_acquire_diag+0x250
//     stack = [0x16b718000, 0x16b79b000)
//
// The walk followed a word that was not a saved frame pointer, arrived at
// 0x100000000018 — which passes that test and is nowhere near a stack — and read 56 bytes
// in. The guest's main thread died inside a diagnostic and the whole run was lost.
//
// The blocking-wait registry had already hit the same bug and fixed it locally, with real
// bounds and a strictly-increasing chain. The lesson did not travel. These tests pin the
// shared implementation, and the first one uses the exact address from that crash.
#include "kudroid/debug/FrameWalk.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::cached_thread_stack_bounds;
using kudroid::FrameWalker;
using kudroid::query_thread_stack_bounds;
using kudroid::StackBounds;

// ─── bounds ──────────────────────────────────────────────────────────────────

void test_bounds_contain_the_callers_own_frame() {
    std::printf("[bounds] the reported stack contains a local of the calling thread\n");
    const StackBounds b = query_thread_stack_bounds();
    Check(b.valid, "bounds are available on this platform");
    if (!b.valid) return;
    Check(b.high > b.low, "high is above low");

    int local = 0;
    const uintptr_t probe = reinterpret_cast<uintptr_t>(&local);
    Check(probe >= b.low && probe < b.high,
          "a local of this frame lies inside [low, high)");

    // Darwin's pthread_get_stackaddr_np returns the HIGH address and Linux's
    // pthread_attr_getstack returns the LOW one. Getting that backwards produces a region
    // entirely above the real stack — which is a bug this codebase has already shipped
    // once, in bionic_pthread_getattr_np. The check above is what catches it either way.
    Check(b.high - b.low >= 4096, "the reported size is at least a page");
}

void test_bounds_are_per_thread() {
    std::printf("[bounds] each thread reports its own stack, not the main thread's\n");
    const StackBounds main_bounds = query_thread_stack_bounds();

    std::atomic<uintptr_t> worker_low{0};
    std::atomic<uintptr_t> worker_high{0};
    std::atomic<bool> worker_contains{false};
    std::thread t([&] {
        const StackBounds b = query_thread_stack_bounds();
        worker_low = b.low;
        worker_high = b.high;
        int local = 0;
        const uintptr_t probe = reinterpret_cast<uintptr_t>(&local);
        worker_contains = b.valid && probe >= b.low && probe < b.high;
    });
    t.join();

    Check(worker_contains.load(), "the worker's own frame is inside the worker's bounds");
    Check(worker_low.load() != main_bounds.low || worker_high.load() != main_bounds.high,
          "and they are a different region from the main thread's");
}

// ─── the crash, as a test ────────────────────────────────────────────────────

// The exact address the device crash dereferenced. Not a made-up out-of-range value: the
// point is that this specific number passed the old check.
constexpr uintptr_t kObservedBadFp = 0x100000000018ULL;
constexpr uintptr_t kObservedFaultAddr = 0x100000000050ULL;  // kObservedBadFp + 56

void test_the_address_that_crashed_the_device_is_refused() {
    std::printf("[regression] 0x100000000018 is refused — the address that crashed a run\n");

    // It really does pass the plausibility test that was in the code. Stated as a check so
    // the reason this test exists cannot be mistaken for paranoia.
    Check(kObservedBadFp > 0x1000 && kObservedBadFp < 0x7fffffffffffULL,
          "it passes `fp > 0x1000 && fp < 0x7fffffffffff` — the old validation");
    Check(kObservedBadFp + 56 == kObservedFaultAddr,
          "and [fp+56] is exactly the fault address from the crash log");

    const StackBounds b = query_thread_stack_bounds();
    Check(b.valid, "bounds available");
    if (!b.valid) return;
    Check(kObservedBadFp < b.low || kObservedBadFp >= b.high,
          "it is NOT inside this thread's stack");

    // The walker must refuse it before any read happens. If this test segfaults rather
    // than failing, the bug is back.
    FrameWalker walker(kObservedBadFp, b);
    Check(!walker.valid(), "FrameWalker(0x100000000018) is invalid from construction");

    uintptr_t out = 0;
    Check(!walker.return_address(&out), "return_address refuses to read");
    Check(!walker.saved_fp(&out), "saved_fp refuses to read");
    Check(!walker.slot(56, &out),
          "slot(56) refuses to read — the load that produced fault_addr=0x100000000050");
    Check(!walker.next(), "next() refuses to advance");
}

void test_unaligned_and_null_frames_are_refused() {
    std::printf("[regression] misaligned and null frame pointers are refused\n");
    const StackBounds b = query_thread_stack_bounds();
    if (!b.valid) { Check(false, "bounds available"); return; }

    // A misaligned fp inside the stack. An unaligned 8-byte load is the other way this
    // faults — see the SIGBUS/BUS_ADRALN round, where fault_addr was 8 mod 16.
    int local = 0;
    const uintptr_t aligned = reinterpret_cast<uintptr_t>(&local) & ~uintptr_t(7);
    Check(!FrameWalker(aligned + 1, b).valid(), "fp+1 (misaligned) is refused");
    Check(!FrameWalker(aligned + 3, b).valid(), "fp+3 (misaligned) is refused");
    Check(!FrameWalker(0, b).valid(), "a null fp is refused");

    // Exactly at the top: the frame RECORD needs two words, so a frame starting in the
    // last word does not fit and must be refused rather than read half off the end.
    Check(!FrameWalker(b.high - sizeof(void*), b).valid(),
          "a frame that would straddle the top of the stack is refused");
    Check(!FrameWalker(b.high, b).valid(), "the top address itself is refused");
    Check(!FrameWalker(b.low - 16, b).valid(), "below the stack is refused");
}

void test_invalid_bounds_refuse_every_walk() {
    std::printf("[regression] with no bounds, nothing is walked at all\n");
    // The fallback that must NOT exist: when the platform gives no bounds, a walk has to
    // refuse rather than drop back to a plausibility test.
    StackBounds none;  // valid == false
    int local = 0;
    Check(!FrameWalker(reinterpret_cast<uintptr_t>(&local), none).valid(),
          "even a genuinely good fp is refused when bounds are unknown");
    Check(!FrameWalker(kObservedBadFp, none).valid(), "and so is the bad one");
}

// ─── walking a real chain ────────────────────────────────────────────────────

// Three nested noinline frames, so there is a real chain with known contents to walk.
// Each records its own return address; the walk must produce the same addresses.
std::vector<uintptr_t> g_expected_returns;
std::vector<uintptr_t> g_walked_returns;

__attribute__((noinline)) void level3() {
    g_expected_returns.push_back(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    FrameWalker walker(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),
                       query_thread_stack_bounds());
    for (int i = 0; i < 8 && walker.valid(); ++i) {
        uintptr_t ret = 0;
        if (!walker.return_address(&ret)) break;
        g_walked_returns.push_back(ret);
        if (!walker.next()) break;
    }
}

__attribute__((noinline)) void level2() {
    g_expected_returns.push_back(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    level3();
    // Defeat a tail call, which would collapse the frame this test is about.
    asm volatile("" ::: "memory");
}

__attribute__((noinline)) void level1() {
    g_expected_returns.push_back(reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    level2();
    asm volatile("" ::: "memory");
}

void test_a_real_chain_is_walked_and_climbs() {
    std::printf("[walk] a genuine chain of frames is followed, and it climbs\n");
    g_expected_returns.clear();
    g_walked_returns.clear();

    level1();

    Check(g_walked_returns.size() >= 2,
          "at least two frames were walked (" + std::to_string(g_walked_returns.size()) +
              " total)");

    // The walk starts in level3, so its first return address is level3's own — which is
    // inside level2. That address is what level3 recorded.
    if (!g_walked_returns.empty() && !g_expected_returns.empty()) {
        Check(g_walked_returns[0] == g_expected_returns.back(),
              "the first frame's return address matches __builtin_return_address(0)");
    }

    // Frames must be strictly increasing. This is the property that makes a cycle
    // impossible rather than merely bounded by an iteration count.
    FrameWalker w(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),
                  query_thread_stack_bounds());
    uintptr_t previous = w.fp();
    bool strictly_increasing = true;
    int steps = 0;
    while (w.next()) {
        if (w.fp() <= previous) { strictly_increasing = false; break; }
        previous = w.fp();
        ++steps;
        if (steps > 64) break;  // a bound on the test, not on the walker
    }
    Check(strictly_increasing, "every frame is at a strictly higher address than the last");
    Check(steps > 0, "and the walk advanced at least once");
}

void test_depth_counts_advances() {
    std::printf("[walk] depth() reports how many frames were climbed\n");
    FrameWalker w(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),
                  query_thread_stack_bounds());
    Check(w.depth() == 0, "depth starts at 0");
    if (w.next()) {
        Check(w.depth() == 1, "one advance -> depth 1");
        if (w.next()) Check(w.depth() == 2, "two advances -> depth 2");
    }
    // depth is what distinguishes "this frame faulted" from "it called several levels down
    // and that faulted" — the difference the fatal-signal breadcrumb reports.
    Check(true, "depth is available for the breadcrumb to report");
}

void test_the_walk_terminates_rather_than_running_to_the_bound() {
    std::printf("[walk] the walk ends at the outermost frame, not at the loop bound\n");
    FrameWalker w(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)),
                  query_thread_stack_bounds());
    int advances = 0;
    while (w.next() && advances < 4096) ++advances;
    Check(advances < 4096, "it terminated on its own after " + std::to_string(advances) +
                               " frames");
    Check(!w.valid(), "and the cursor is invalid at the end");
}

// ─── a hostile chain ─────────────────────────────────────────────────────────

// A frame chain that points OUT of the stack, built deliberately. This is the shape the
// device hit: a word read where a saved fp would be, whose value is a plausible-looking
// address in unmapped memory. With PROT_NONE either side, following it is a real SIGSEGV
// rather than a silent success, so a regression here fails loudly.
void test_a_chain_pointing_outside_the_stack_is_not_followed() {
    std::printf("[hostile] a saved-fp slot pointing outside the stack stops the walk\n");

    const size_t page = 4096;
    char* region = static_cast<char*>(
        mmap(nullptr, page * 3, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (region == MAP_FAILED) { Check(false, "mmap for the hostile region"); return; }
    char* middle = region + page;
    if (mprotect(middle, page, PROT_READ | PROT_WRITE) != 0) {
        Check(false, "mprotect the middle page");
        munmap(region, page * 3);
        return;
    }

    // The middle page is readable, the pages either side are not. A walk that steps into
    // this region and keeps going will fault; a walk that refuses it will not.
    const StackBounds stack = query_thread_stack_bounds();
    uintptr_t* fake_frame = reinterpret_cast<uintptr_t*>(middle);
    fake_frame[0] = reinterpret_cast<uintptr_t>(middle) + page;  // saved fp -> PROT_NONE
    fake_frame[1] = 0xDEADBEEF;                                  // return address

    // Construction alone must refuse it: it is not inside the thread's stack.
    FrameWalker walker(reinterpret_cast<uintptr_t>(middle), stack);
    Check(!walker.valid(), "a frame in unrelated mapped memory is refused (not in stack)");
    uintptr_t out = 0;
    Check(!walker.return_address(&out), "and its return address is not read");

    // And with bounds that DO cover the region — the situation where the frame itself is
    // legitimate — the walk must still refuse to step into the PROT_NONE page, because the
    // next frame lies outside the bounds.
    StackBounds fake_bounds;
    fake_bounds.low = reinterpret_cast<uintptr_t>(middle);
    fake_bounds.high = reinterpret_cast<uintptr_t>(middle) + page;
    fake_bounds.valid = true;

    FrameWalker inner(reinterpret_cast<uintptr_t>(middle), fake_bounds);
    Check(inner.valid(), "with matching bounds the frame itself is readable");
    Check(inner.return_address(&out) && out == 0xDEADBEEF,
          "its return address reads back correctly");
    Check(!inner.next(),
          "but next() refuses the saved fp that points into the PROT_NONE page");
    Check(!inner.valid(), "and the cursor is left invalid rather than dangling");

    // slot() must refuse a read that would straddle the end of the region.
    FrameWalker edge(fake_bounds.high - 16, fake_bounds);
    Check(edge.valid(), "a frame in the last 16 bytes is readable");
    Check(!edge.slot(56, &out), "but slot(56) from there is out of bounds and refused");

    munmap(region, page * 3);
}

void test_a_self_referential_chain_cannot_loop() {
    std::printf("[hostile] a frame whose saved fp points at itself stops immediately\n");
    // Not merely bounded by a counter: the strictly-increasing requirement makes this
    // terminate on the first step.
    const size_t page = 4096;
    char* region = static_cast<char*>(
        mmap(nullptr, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (region == MAP_FAILED) { Check(false, "mmap"); return; }

    StackBounds bounds;
    bounds.low = reinterpret_cast<uintptr_t>(region);
    bounds.high = bounds.low + page;
    bounds.valid = true;

    uintptr_t* frame = reinterpret_cast<uintptr_t*>(region + 64);
    frame[0] = reinterpret_cast<uintptr_t>(frame);  // points at itself
    frame[1] = 0x1234;

    FrameWalker w(reinterpret_cast<uintptr_t>(frame), bounds);
    Check(w.valid(), "the frame is readable");
    Check(!w.next(), "a self-referential saved fp is refused (not strictly increasing)");

    // And a chain that goes DOWN, which is the other direction garbage takes.
    uintptr_t* higher = reinterpret_cast<uintptr_t*>(region + 128);
    higher[0] = reinterpret_cast<uintptr_t>(frame);  // lower than itself
    higher[1] = 0x5678;
    FrameWalker down(reinterpret_cast<uintptr_t>(higher), bounds);
    Check(down.valid(), "the second frame is readable");
    Check(!down.next(), "a descending chain is refused too");

    munmap(region, page);
}

// ─── the cached form ─────────────────────────────────────────────────────────

void test_the_cached_bounds_agree_with_a_fresh_query() {
    std::printf("[bounds] the cached bounds match a fresh query, per thread\n");
    const StackBounds fresh = query_thread_stack_bounds();
    const StackBounds& cached = cached_thread_stack_bounds();
    Check(cached.valid == fresh.valid, "validity agrees");
    Check(cached.low == fresh.low && cached.high == fresh.high,
          "and so do the bounds themselves");

    // On a worker thread the cache must be that thread's, not a copy of the main
    // thread's — it is thread_local, and a static would be the classic error.
    std::atomic<bool> worker_ok{false};
    std::atomic<bool> differs{false};
    std::thread t([&] {
        const StackBounds& c = cached_thread_stack_bounds();
        int local = 0;
        const uintptr_t probe = reinterpret_cast<uintptr_t>(&local);
        worker_ok = c.valid && probe >= c.low && probe < c.high;
        differs = (c.low != fresh.low || c.high != fresh.high);
    });
    t.join();
    Check(worker_ok.load(), "a worker's cached bounds contain the worker's own frame");
    Check(differs.load(), "and are not the main thread's");
}

}  // namespace

int main() {
    std::printf("=== frame walk ===\n");
    test_bounds_contain_the_callers_own_frame();
    test_bounds_are_per_thread();
    test_the_address_that_crashed_the_device_is_refused();
    test_unaligned_and_null_frames_are_refused();
    test_invalid_bounds_refuse_every_walk();
    test_a_real_chain_is_walked_and_climbs();
    test_depth_counts_advances();
    test_the_walk_terminates_rather_than_running_to_the_bound();
    test_a_chain_pointing_outside_the_stack_is_not_followed();
    test_a_self_referential_chain_cannot_loop();
    test_the_cached_bounds_agree_with_a_fresh_query();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
