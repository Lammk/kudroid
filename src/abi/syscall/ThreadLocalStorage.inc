extern "C" int bionic_pthread_mutexattr_init(void* attr) {
    // Must clear the kind bits. A guest typically declares pthread_mutexattr_t on
    // the stack, so leaving it untouched let pthread_mutex_init read stack garbage
    // as the mutex kind — a NORMAL mutex became RECURSIVE or vice versa depending
    // on what happened to be in that frame. Only the low 32 bits are written: the
    // shim reads nothing above them, and bionic's own type is wider.
    auto* p = static_cast<uint32_t*>(attr);
    if (!p) return EINVAL;
    *p = 0;  // PTHREAD_MUTEX_DEFAULT
    return 0;
}
extern "C" int bionic_pthread_mutexattr_destroy(void* attr) { (void)attr; return 0; }
extern "C" int bionic_pthread_mutexattr_settype(void* attr, int type) {
    // Store the kind in the guest attr's low 2 bits.
    auto* p = static_cast<uint32_t*>(attr);
    if (!p) return -1;
    *p = (*p & ~0x3u) | (static_cast<uint32_t>(type) & 0x3u);
    return 0;
}

extern "C" int bionic_pthread_mutexattr_gettype(void* attr, int* type) {
    const auto* p = static_cast<const uint32_t*>(attr);
    if (!p || !type) return EINVAL;
    *type = static_cast<int>(*p & 0x3u);
    return 0;
}


extern "C" int bionic_pthread_mutex_trylock(void* guestMutex) {
    HostMutex* host = host_mutex_for(guestMutex);
    if (!host) return EINVAL;
    const int rc = ::pthread_mutex_trylock(&host->mutex);
    if (rc == 0 && host->track_owner) {
        host->owner.store(current_thread_id(), std::memory_order_relaxed);
    }
    return rc;
}

extern "C" int bionic_pthread_key_create(void* guestKey, void (*destructor)(void*)) {
    if (!guestKey) return -1;
    pthread_key_t hostKey;
    int res = ::pthread_key_create(&hostKey, destructor);
    if (res == 0) {
        // Bionic pthread_key_t is a 32-bit int, while Darwin's pthread_key_t is
        // 64-bit. memcpy(sizeof(pthread_key_t)) would overflow the guest's
        // 4-byte key slot. Darwin keys are small integers, so truncation is safe.
        *static_cast<int*>(guestKey) = static_cast<int>(hostKey);
    }
    return res;
}
extern "C" void* bionic_pthread_getspecific(int guestKey) {
    return ::pthread_getspecific(static_cast<pthread_key_t>(guestKey));
}
extern "C" int bionic_pthread_setspecific(int guestKey, const void* value) {
    return ::pthread_setspecific(static_cast<pthread_key_t>(guestKey), value);
}
extern "C" int bionic_pthread_key_delete(int guestKey) {
    return ::pthread_key_delete(static_cast<pthread_key_t>(guestKey));
}

// Bionic pthread_once: 0 = not run, 1 = running, 2 = done. Per-word mutex
// avoids deadlock on nested calls with different control words.
struct BionicOnceControl {
    std::mutex mtx;
    std::condition_variable cv;
};
static std::mutex g_once_map_mtx;
static std::unordered_map<int*, std::shared_ptr<BionicOnceControl>> g_once_controls;

static int once_state_load(int* guest_once) {
    return __atomic_load_n(guest_once, __ATOMIC_ACQUIRE);
}

static void once_state_store(int* guest_once, int state) {
    __atomic_store_n(guest_once, state, __ATOMIC_RELEASE);
}

extern "C" int bionic_pthread_once(int* guest_once, void (*init_routine)(void)) {
    if (!guest_once || !init_routine) return -1;

    // Fast path: already done.
    if (once_state_load(guest_once) == 2) return 0;

    std::shared_ptr<BionicOnceControl> ctl;
    {
        std::lock_guard<std::mutex> lock(g_once_map_mtx);
        auto& slot = g_once_controls[guest_once];
        if (!slot) slot = std::make_shared<BionicOnceControl>();
        ctl = slot;
    }

    std::unique_lock<std::mutex> lock(ctl->mtx);
    // Another thread is running init for this word → wait.
    if (once_state_load(guest_once) == 1) {
        // An initialiser that never finishes parks every other thread that needs the
        // same static, and nothing else in the log would say which one.
        const BlockingWaitScope tracked(WaitKind::kOnce, guest_once,
                                        guest_return_address(6));
        while (once_state_load(guest_once) == 1) {
            ctl->cv.wait(lock);
        }
    }
    // Another thread finished while we waited.
    if (once_state_load(guest_once) == 2) return 0;

    // We run first: mark in-progress and run init; other words use other mutexes.
    once_state_store(guest_once, 1);
    init_routine();
    once_state_store(guest_once, 2);
    ctl->cv.notify_all();

    // Best-effort map cleanup; later calls hit the fast path.
    std::lock_guard<std::mutex> mapLock(g_once_map_mtx);
    auto it = g_once_controls.find(guest_once);
    if (it != g_once_controls.end() && it->second == ctl) {
        g_once_controls.erase(it);
    }
    return 0;
}

#include <signal.h>

// Guest signal handling lives in GuestSignals.cpp.
//
// It used to live here, in a `struct android_sigaction` that declared bionic's ILP32
// field order on an LP64 guest. Both layouts are 32 bytes, so nothing failed a size
// check; what happened instead is that sa_flags was read as the handler pointer.
// ULTRAKILL's main thread ended up executing at pc=0x18000004 — which is
// SA_SIGINFO|SA_ONSTACK|SA_RESTART, the flags the guest had passed — spinning at 100%
// of one core inside _sigtramp, because every fault re-entered the same bad handler.
// The same bug also replaced KuDroid's SIGSEGV handler with that value, which is why
// the run produced no crash log at all.
//
// Three separate translations are needed and none of them is optional: the struct
// layout, the signal NUMBER (Linux and Darwin diverge after SIGFPE — guest SIGUSR1 is
// 10, which is Darwin's SIGBUS), and ownership of the signals KuDroid needs to keep
// working (SIGTRAP supplies guest TLS, SIGSYS emulates a raw `svc`). Doing that in one
// place, with the layouts pinned by static_assert, is the point.
extern "C" int bionic_sigaction(int signum, const void* act, void* oldact) {
    return kudroid::guest_sigaction(signum,
                                    static_cast<const kudroid::GuestSigaction*>(act),
                                    static_cast<kudroid::GuestSigaction*>(oldact));
}

// rt_sigaction is what a guest reaches through syscall(). Same call: bionic's
// sigaction() is a thin wrapper over it, and the sigset size argument only matters for
// ILP32, where sigset_t is too small for the real-time signals.
extern "C" int bionic_rt_sigaction(int signum, const void* act, void* oldact,
                                   size_t sigsetsize) {
    if (sigsetsize != sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }
    return bionic_sigaction(signum, act, oldact);
}

static pthread_key_t tls_key;
static pthread_once_t tls_key_once = PTHREAD_ONCE_INIT;

static void tls_destructor(void* tls_base) {
    if (tls_base) {
        std::free(tls_base);
    }
}

static void init_tls_key() {
    ::pthread_key_create(&tls_key, tls_destructor);
}

// Guest TLS block size and bionic offsets (arm64).
// Thread pointer (tpidr_el0) points into the slot area; slot N is tpidr + N*8.
constexpr size_t kTlsBlockSize    = 65536;
constexpr size_t kTlsSlotOffset   = 32768; // TP = tls_base + kTlsSlotOffset
constexpr size_t kTlsModuleOffset = 4096;  // guest TLS template offset relative to TP
constexpr size_t kTlsStackGuardSlotOffset = 40; // slot 5

// Guest module TLS template (PT_TLS), registered by elf_loader after mapping.
static const void* g_tls_template = nullptr;
static size_t g_tls_template_size = 0;
static std::mutex g_tls_template_mtx;

extern "C" void kudroid_tls_set_template(const void* tls_template, size_t tls_filesz) {
    std::lock_guard<std::mutex> lock(g_tls_template_mtx);
    g_tls_template = tls_template;
    g_tls_template_size = tls_filesz;
}

extern "C" size_t kudroid_tls_module_offset(void) {
    return kTlsModuleOffset;
}

// Allocate a guest TLS block: zero it, copy the TLS template, set guard cookie.
// Shared by the main thread, new threads, and lazy allocation in the trap handler.
static void* alloc_guest_tls_block(void) {
    void* tls_base = std::aligned_alloc(16, kTlsBlockSize);
    if (!tls_base) return nullptr;
    std::memset(tls_base, 0, kTlsBlockSize);

    {
        std::lock_guard<std::mutex> lock(g_tls_template_mtx);
        if (g_tls_template && g_tls_template_size > 0 &&
            g_tls_template_size <= kTlsBlockSize - kTlsSlotOffset - kTlsModuleOffset) {
            std::memcpy(static_cast<char*>(tls_base) + kTlsSlotOffset + kTlsModuleOffset,
                        g_tls_template, g_tls_template_size);
        }
    }

    // Stack guard cookie at slot 5 (offset 40 from TP).
    *reinterpret_cast<uint64_t*>(static_cast<char*>(tls_base) + kTlsSlotOffset + kTlsStackGuardSlotOffset) =
        kStackGuardCookie;
    return tls_base;
}

struct BionicThreadArgs {
    void* (*start_routine)(void*);
    void* arg;
};

static void* bionic_thread_wrapper(void* rawArgs);

// Match Android's bionic FPCR for all guest threads: FZ (bit 24) flushes
// denormals to zero and DN (bit 25) collapses NaN payloads to the Default NaN.
// Darwin leaves both clear, so guest SIMD code written against bionic
// semantics — denormal-flushed, NaN-quieting — observes values Android never
// produces (observed shape: rank corruption in PhysX radix sorts after
// denormal-bearing SIMD ops). Only these two bits are set; sticky IDC/IXC/Ofc
// cumulative flags are preserved untouched. Runs on the thread itself: MSR
// FPCR is not signal-handler safe and this must apply before guest code.
// Defined before both callers (bionic_init_main_thread_tls here,
// bionic_thread_wrapper below) — a use-before-declaration that only the
// Apple arm64 CI saw, because every other host compiles the block out.
#if defined(__aarch64__)
static void apply_guest_fpcr(void) {
    uint64_t fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24) | (1ULL << 25);  // FZ | DN
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
}
#endif

extern "C" void bionic_init_main_thread_tls(void) {
#if defined(__aarch64__)
    apply_guest_fpcr();
#endif
    ::pthread_once(&tls_key_once, init_tls_key);
    if (::pthread_getspecific(tls_key)) return; // already has TLS
    void* tls_base = alloc_guest_tls_block();
    if (!tls_base) return;
    
    char* tls_ptr = static_cast<char*>(tls_base) + kTlsSlotOffset;
    (void)tls_ptr;
    
    ::pthread_setspecific(tls_key, tls_base);

#if defined(__aarch64__)
    // Read BEFORE
    uint64_t old_tpidr = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(old_tpidr));
    
    // Write new value
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(tls_ptr));
    
    // Read AFTER to verify
    uint64_t new_tpidr = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(new_tpidr));
    
    // Check the stack guard is readable at offset 40
    uint64_t guard_check = *reinterpret_cast<uint64_t*>(new_tpidr + 40);
    
    // Diagnostic (gated): runs per new thread — log only when investigating TLS.
    if (guard_diag_enabled()) {
        fprintf(stderr, "[TLS_DIAG] tls_base=%p tls_ptr=%p\n", reinterpret_cast<void*>(tls_base), reinterpret_cast<void*>(tls_ptr));
        fprintf(stderr, "[TLS_DIAG] tpidr_el0 BEFORE=0x%llx AFTER=0x%llx\n", 
                (unsigned long long)old_tpidr, (unsigned long long)new_tpidr);
        fprintf(stderr, "[TLS_DIAG] stack_guard@offset40=0x%llx (expect 0x1337BEEFCAFECAFE)\n",
                (unsigned long long)guard_check);
        fprintf(stderr, "[TLS_DIAG] write %s\n", 
                (new_tpidr == (uint64_t)tls_ptr) ? "SUCCESS" : "FAILED");
    }
#endif
}

// Where a thread's guest TLS pointer can be read without a trap: the byte offset of
// the TLS key's slot in the thread-specific-data array that tpidrro_el0 points at, and
// the offset to add to the stored block to get the guest thread pointer. The host
// kernel does not preserve tpidr_el0, so this is the only per-thread state a guest TLS
// read can rely on. Returns 0 on success, -1 where it cannot be determined.
extern "C" int kudroid_tls_stub_params(int64_t* tsdSlotOffset, int64_t* tlsPointerOffset) {
#if defined(__APPLE__) && defined(__aarch64__)
    if (!tsdSlotOffset || !tlsPointerOffset) return -1;
    ::pthread_once(&tls_key_once, init_tls_key);
    void* const previous = ::pthread_getspecific(tls_key);
    const uintptr_t sentinel = 0x5a5a4321cafef00dull;
    if (::pthread_setspecific(tls_key, reinterpret_cast<void*>(sentinel)) != 0) return -1;
    uintptr_t base;
    __asm__ volatile("mrs %0, tpidrro_el0" : "=r"(base));
    const auto* tsd = reinterpret_cast<const volatile uintptr_t*>(base & ~uintptr_t(7));
    int64_t found = -1;
    for (int i = 0; i < 520; ++i) {
        if (tsd[i] == sentinel) { found = static_cast<int64_t>(i) * 8; break; }
    }
    ::pthread_setspecific(tls_key, previous);
    if (found < 0 || found > 32760) return -1;
    *tsdSlotOffset = found;
    *tlsPointerOffset = static_cast<int64_t>(kTlsSlotOffset);
    return 0;
#else
    (void)tsdSlotOffset; (void)tlsPointerOffset;
    return -1;
#endif
}

} // namespace
} // namespace kudroid

namespace kudroid {
bool bionic_handle_tpidr_trap(void* ucontext) {
    (void)ucontext;
#if defined(__APPLE__) && defined(__aarch64__)
    if (!ucontext) return false;
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    uint32_t* pc = reinterpret_cast<uint32_t*>(uc->uc_mcontext->__ss.__pc);
    uint32_t inst = *pc;
    
    // Check if it's our BRK #(0x1000 + N) instruction
    // BRK encoding: 1101 0100 001i iiii iiii iiii iii0 0000 -> 0xD4200000 | (imm16 << 5)
    if ((inst & 0xFFE0001F) == 0xD4200000) {
        uint32_t imm16 = (inst >> 5) & 0xFFFF;
        if (imm16 >= 0x1000 && imm16 < 0x1020) { // 0x1000 to 0x101F
            uint32_t reg = imm16 - 0x1000;

            ::pthread_once(&tls_key_once, init_tls_key);
            void* tls_base = ::pthread_getspecific(tls_key);
            if (!tls_base) {
                // Lazy allocation for host-created threads running guest code.
                // Synchronous trap, so malloc here is safe; freed at thread exit.
                tls_base = alloc_guest_tls_block();
                if (tls_base) {
                    ::pthread_setspecific(tls_key, tls_base);
                }
            }
            char* tls_ptr = tls_base ? (static_cast<char*>(tls_base) + kTlsSlotOffset) : nullptr;
            
            // Write the TLS pointer into the faulting thread's register state
            uc->uc_mcontext->__ss.__x[reg] = reinterpret_cast<uint64_t>(tls_ptr);
            
            // Advance PC to skip the BRK instruction
            uc->uc_mcontext->__ss.__pc += 4;
            return true;
        }
    }
#endif
    return false;
}
} // namespace kudroid

namespace kudroid {
bool bionic_handle_jit26_trap(void* ucontext) {
    (void)ucontext;
#if defined(__APPLE__) && defined(__aarch64__)
    if (!ucontext) return false;
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    uint32_t* pc = reinterpret_cast<uint32_t*>(uc->uc_mcontext->__ss.__pc);
    const uint32_t inst = *pc;
    // BRK encoding: 0xD4200000 | (imm16 << 5). The TXM JIT protocol uses
    // imm16 == 0xf00d; KuDroid's TLS emulation uses 0x1000..0x101f.
    if ((inst & 0xFFE0001F) == 0xD4200000 && ((inst >> 5) & 0xFFFF) == 0xf00d) {
        // The debug script services this stop while attached, so reaching here
        // means no script is present. Report the operation as unprepared
        // (x0 = 0) and step over the breakpoint; a run without JIT then
        // degrades instead of dying on a trap it never intended to service.
        uc->uc_mcontext->__ss.__x[0] = 0;
        uc->uc_mcontext->__ss.__pc += 4;
        return true;
    }
#endif
    return false;
}

bool bionic_handle_sysreg_trap(void* ucontext) {
    (void)ucontext;
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
    if (!ucontext) return false;
    ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
    uintptr_t pc_val = uc->uc_mcontext->__ss.__pc;
    if ((pc_val & 3) != 0) return false;
    uint32_t* pc = reinterpret_cast<uint32_t*>(pc_val);
    const uint32_t inst = *pc;

    // ARM64 system instructions: MRS / MSR encoding
    if ((inst & 0xFFC00000) == 0xD5000000) {
        if ((inst & 0x00200000) == 0) {
            // MSR write to restricted system register: step over
            uc->uc_mcontext->__ss.__pc += 4;
            return true;
        }

        const uint32_t sysreg = inst & 0xFFFFFFE0;
        const uint32_t rt = inst & 0x1F;
        uint64_t val = 0;

        switch (sysreg) {
            case 0xD53B0020: { // CTR_EL0 (Cache Type Register)
                static uint64_t s_ctrVal = 0;
                if (s_ctrVal == 0) {
                    size_t line_size = 64;
                    size_t len = sizeof(line_size);
                    if (sysctlbyname("hw.cachelinesize", &line_size, &len, nullptr, 0) != 0 || line_size == 0) {
                        line_size = 64;
                    }
                    uint32_t words = static_cast<uint32_t>(line_size / 4);
                    uint32_t log2_words = 0;
                    while ((1u << log2_words) < words) {
                        log2_words++;
                    }
                    if (log2_words < 3) log2_words = 3;
                    if (log2_words > 6) log2_words = 6;
                    s_ctrVal = (1ULL << 31) |
                               (static_cast<uint64_t>(log2_words) << 24) |
                               (static_cast<uint64_t>(log2_words) << 20) |
                               (static_cast<uint64_t>(log2_words) << 16) |
                               (3ULL << 14) |
                               (static_cast<uint64_t>(log2_words));
                }
                val = s_ctrVal;
                break;
            }
            case 0xD53B00E0: // DCZID_EL0 (Data Cache Zero ID)
                val = 0x00000004ULL;
                break;
            case 0xD5380000: // MIDR_EL1 (Main ID)
                val = 0x410FD030ULL;
                break;
            case 0xD53800A0: // MPIDR_EL1 (Multiprocessor Affinity)
                val = 0x80000000ULL;
                break;
            case 0xD53800C0: // REVIDR_EL1
                val = 0ULL;
                break;
            case 0xD5380400: // ID_AA64PFR0_EL1
                val = 0x0000000000001111ULL;
                break;
            case 0xD5380600: // ID_AA64ISAR0_EL1
                val = 0x00011120ULL;
                break;
            case 0xD5380620: // ID_AA64ISAR1_EL1
                val = 0ULL;
                break;
            default:
                val = 0ULL;
                break;
        }

        if (rt < 29) {
            uc->uc_mcontext->__ss.__x[rt] = val;
        } else if (rt == 29) {
            uc->uc_mcontext->__ss.__fp = val;
        } else if (rt == 30) {
            uc->uc_mcontext->__ss.__lr = val;
        }
        uc->uc_mcontext->__ss.__pc += 4;
        return true;
    }
#endif
    return false;
}
} // namespace kudroid

