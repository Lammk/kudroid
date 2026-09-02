#include "kudroid/debug/ThreadSampler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <pthread.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/mach_port.h>
#include <mach/task.h>
#include <mach/thread_act.h>
#include <mach/thread_info.h>
#include <mach/thread_status.h>
#include <mach/vm_map.h>
#endif

extern "C" void kudroid_persistent_breadcrumb(const char* line);

namespace kudroid {

// Resolving an address to "module+offset" lives in elf_loader. Declared rather than
// included so this file does not pull in the loader's headers.
extern "C" bool kudroid_lookup_guest_module(void* addr, char* out, std::size_t outSize);

namespace {

// Everything here goes to native_breadcrumbs.log and NOT to the Android log.
//
// kudroid_android_log_message takes a process-wide mutex and does an fopen/fclose per
// line. Both are wrong for this: a thread that wedged while logging holds that mutex,
// and the sampler would block on it — inside the diagnostic meant to explain the
// wedge. A breadcrumb is one O_APPEND write with no lock at all, which is what makes
// it safe to call while the process is in an unknown state. The file is already part
// of what gets collected from a device.
void emit(const char* line) { kudroid_persistent_breadcrumb(line); }

#if defined(__APPLE__) && defined(__aarch64__)

// Upper bound on threads reported in one sample. A guest engine runs tens; a hundred
// and twenty-eight is far past that, and a fixed array is what keeps this allocation
// free — allocating here would mean taking the allocator's lock, which is exactly the
// lock a wedged thread is most likely to be holding.
constexpr int kMaxThreads = 128;

// What was read out of one thread, before anything is symbolized.
//
// The raw pass fills these and writes them out; symbolization happens afterwards
// from this array. See the two-pass note in thread_sample_report.
struct RawThread {
    uint64_t tid;
    uint64_t pc;
    uint64_t lr;
    uint64_t sp;
    uint64_t fp;
    uint64_t cpu_ms;
    int state;
    int suspend_count;
    bool have_regs;
    bool is_self;
    char name[64];
};

// Mach thread run states, as text. A number here would need a lookup every time
// someone reads the log, and the whole value of this line is being readable at a
// glance: "running with CPU time climbing" and "waiting with CPU time flat" are
// different bugs.
const char* run_state_name(int state) {
    switch (state) {
        case TH_STATE_RUNNING: return "running";
        case TH_STATE_STOPPED: return "stopped";
        case TH_STATE_WAITING: return "waiting";  // parked, interruptible
        case TH_STATE_UNINTERRUPTIBLE: return "uninterruptible";
        case TH_STATE_HALTED: return "halted";
        default: return "?";
    }
}

// Describe one address: a guest module and offset when it is guest code, a host image
// and symbol when dyld knows it, otherwise the bare pointer.
void describe_address(uint64_t addr, char* out, size_t outSize) {
    if (addr == 0) {
        std::snprintf(out, outSize, "0");
        return;
    }
    char module[256];
    if (kudroid_lookup_guest_module(reinterpret_cast<void*>(addr), module, sizeof(module))) {
        // The loader already formats this as "0x<addr> <path>+0x<off>".
        std::snprintf(out, outSize, "%s", module);
        return;
    }
    Dl_info info;
    std::memset(&info, 0, sizeof(info));
    if (dladdr(reinterpret_cast<void*>(addr), &info) != 0 && info.dli_fname != nullptr) {
        const char* slash = std::strrchr(info.dli_fname, '/');
        const char* base = slash != nullptr ? slash + 1 : info.dli_fname;
        // A symbol name beats an offset for a host frame: "__psynch_cvwait" or "poll"
        // names the wait outright, where "libsystem_kernel.dylib+0x1f2c" needs a
        // second tool to interpret.
        if (info.dli_sname != nullptr && info.dli_saddr != nullptr) {
            std::snprintf(out, outSize, "%s`%s+0x%llx", base, info.dli_sname,
                          static_cast<unsigned long long>(
                              addr - reinterpret_cast<uint64_t>(info.dli_saddr)));
        } else {
            std::snprintf(out, outSize, "%s+0x%llx", base,
                          static_cast<unsigned long long>(
                              addr - reinterpret_cast<uint64_t>(info.dli_fbase)));
        }
        return;
    }
    std::snprintf(out, outSize, "unknown");
}

#endif  // __APPLE__ && __aarch64__

[[maybe_unused]] std::atomic<unsigned> g_sequence{0};

}  // namespace

int thread_sample_report(const char* reason) {
#if defined(__APPLE__) && defined(__aarch64__)
    const unsigned seq = g_sequence.fetch_add(1, std::memory_order_relaxed) + 1;

    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS) return 0;

    char header[320];
    std::snprintf(header, sizeof(header), "thread-sample-begin seq=%u reason=%s threads=%u",
                  seq, reason != nullptr ? reason : "?", static_cast<unsigned>(count));
    emit(header);

    // Which port is the sampler's own. The sampling thread is never the interesting
    // one, and marking it keeps a reader from chasing the watchdog's own stack.
    const thread_act_t self = mach_thread_self();

    RawThread raw[kMaxThreads];
    int n = 0;

    // ── Pass one: read and write the numbers, symbolize nothing. ──────────────────
    //
    // Two passes, because symbolization is the one part of this that can block. dladdr
    // takes dyld's lock, and if some thread is wedged inside dyld this would join it —
    // inside the diagnostic meant to explain the wedge. Writing raw pc/lr/sp first
    // means the addresses reach the log even if the second pass never finishes: they
    // are still resolvable afterwards against the module list the loader already
    // prints at startup.
    for (mach_msg_type_number_t i = 0; i < count && n < kMaxThreads; ++i) {
        const thread_act_t thread = threads[i];
        RawThread& t = raw[n];
        std::memset(&t, 0, sizeof(t));
        t.state = -1;
        t.is_self = (thread == self);

        // Identity: THREAD_IDENTIFIER_INFO's thread_id is the same 64-bit number
        // pthread_threadid_np returns, so this line joins to every other diagnostic
        // KuDroid prints — the blocking-wait registry's tid=, the futex line's, and
        // the telemetry watchdog's native_thread_id=.
        thread_identifier_info_data_t ident;
        std::memset(&ident, 0, sizeof(ident));
        mach_msg_type_number_t identCount = THREAD_IDENTIFIER_INFO_COUNT;
        if (thread_info(thread, THREAD_IDENTIFIER_INFO,
                        reinterpret_cast<thread_info_t>(&ident), &identCount) == KERN_SUCCESS) {
            t.tid = ident.thread_id;
        }

        // Run state and CPU time. Together these separate the two failure modes that
        // are indistinguishable from outside: a spin burns time and stays "running", a
        // deadlock sits at "waiting" with its time frozen.
        thread_basic_info_data_t basic;
        std::memset(&basic, 0, sizeof(basic));
        mach_msg_type_number_t basicCount = THREAD_BASIC_INFO_COUNT;
        if (thread_info(thread, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&basic),
                        &basicCount) == KERN_SUCCESS) {
            t.state = basic.run_state;
            t.suspend_count = basic.suspend_count;
            t.cpu_ms = static_cast<uint64_t>(basic.user_time.seconds) * 1000ull +
                       static_cast<uint64_t>(basic.user_time.microseconds) / 1000ull +
                       static_cast<uint64_t>(basic.system_time.seconds) * 1000ull +
                       static_cast<uint64_t>(basic.system_time.microseconds) / 1000ull;
        }

        // The registers. This is the answer nothing else in the process could give.
        arm_thread_state64_t regs;
        std::memset(&regs, 0, sizeof(regs));
        mach_msg_type_number_t regCount = ARM_THREAD_STATE64_COUNT;
        if (thread_get_state(thread, ARM_THREAD_STATE64, reinterpret_cast<thread_state_t>(&regs),
                             &regCount) == KERN_SUCCESS) {
            // The accessors, not the fields: on arm64e pc/lr/sp/fp are signed pointers
            // and reading them raw yields a value with authentication bits set.
            t.pc = arm_thread_state64_get_pc(regs);
            t.lr = arm_thread_state64_get_lr(regs);
            t.sp = arm_thread_state64_get_sp(regs);
            t.fp = arm_thread_state64_get_fp(regs);
            t.have_regs = true;
        }

        // pthread_getname_np needs a pthread_t, and a Mach port maps back to one only
        // for threads created through pthread. When it fails the name is simply
        // absent, which costs little: tid still identifies the thread, and the name is
        // a convenience — though a valuable one, since the guest sets it
        // (prctl(PR_SET_NAME, "UnityMain")) and it says what the thread is for.
        const pthread_t handle = pthread_from_mach_thread_np(thread);
        if (handle != nullptr) {
            if (pthread_getname_np(handle, t.name, sizeof(t.name)) != 0) t.name[0] = '\0';
        }

        char line[512];
        std::snprintf(line, sizeof(line),
                      "thread-sample seq=%u tid=%llu name=%s state=%s suspend=%d cpu_ms=%llu "
                      "pc=0x%llx lr=0x%llx sp=0x%llx fp=0x%llx self=%d",
                      seq, static_cast<unsigned long long>(t.tid),
                      t.name[0] != '\0' ? t.name : "-", run_state_name(t.state),
                      t.suspend_count, static_cast<unsigned long long>(t.cpu_ms),
                      static_cast<unsigned long long>(t.pc),
                      static_cast<unsigned long long>(t.lr),
                      static_cast<unsigned long long>(t.sp),
                      static_cast<unsigned long long>(t.fp), t.is_self ? 1 : 0);
        emit(line);
        ++n;
    }

    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    mach_port_deallocate(mach_task_self(), self);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                  count * sizeof(thread_act_t));

    // ── Pass two: names for the addresses. ───────────────────────────────────────
    //
    // Only for threads whose registers were readable, and only pc and lr: those two
    // say where the thread is and who called it, which is the whole question. The
    // sampler's own thread is skipped — its stack is this function.
    for (int i = 0; i < n; ++i) {
        if (!raw[i].have_regs || raw[i].is_self) continue;
        char pc_text[320];
        char lr_text[320];
        describe_address(raw[i].pc, pc_text, sizeof(pc_text));
        describe_address(raw[i].lr, lr_text, sizeof(lr_text));
        char line[832];
        std::snprintf(line, sizeof(line), "thread-where seq=%u tid=%llu pc=%s lr=%s", seq,
                      static_cast<unsigned long long>(raw[i].tid), pc_text, lr_text);
        emit(line);
    }

    char footer[128];
    std::snprintf(footer, sizeof(footer), "thread-sample-end seq=%u reported=%d", seq, n);
    emit(footer);
    return n;
#else
    // Host builds and non-arm64 Apple: there is no cross-thread register access here.
    // Said once rather than silently returning 0, so a missing sample in a log has an
    // explanation instead of looking like a bug in the sampler.
    static std::atomic<bool> warned{false};
    bool expected = false;
    if (warned.compare_exchange_strong(expected, true)) {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "thread-sample-unavailable reason=%s (no cross-thread register access on "
                      "this platform)",
                      reason != nullptr ? reason : "?");
        emit(line);
    }
    return 0;
#endif
}

}  // namespace kudroid
