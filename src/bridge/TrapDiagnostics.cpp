#include "TrapDiagnostics.h"
#include "kudroid/kudroid_bridge.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <unistd.h>

#if defined(__APPLE__)
#include "kudroid/MachVmDecls.h"
#elif defined(__linux__)
#include <sys/uio.h>
#endif

namespace {
struct TrapSnapshot {
    int signal;
    uint64_t pc, lr, sp;
    uint64_t x[8];
};

struct Slot {
    std::atomic<unsigned> state{0};
    TrapSnapshot snapshot{};
};
static_assert(std::atomic<unsigned>::is_always_lock_free, "Trap capture requires lock-free atomics");
Slot g_slots[4];

struct SignalLine {
    char bytes[384];
    unsigned used = 0;

    void text(const char* value) {
        while (*value && used < sizeof(bytes) - 1) bytes[used++] = *value++;
    }
    void hex(uint64_t value) {
        text("0x");
        for (int shift = 60; shift >= 0; shift -= 4) {
            if (used < sizeof(bytes) - 1) bytes[used++] = "0123456789abcdef"[(value >> shift) & 15];
        }
    }
};

bool ReadMemory(uint64_t address, void* output, size_t length) {
    if (!address || address > UINTPTR_MAX || length > UINTPTR_MAX - address) return false;
#if defined(__APPLE__)
    vm_size_t copied = 0;
    return vm_read_overwrite(mach_task_self(), static_cast<vm_address_t>(address), length,
        reinterpret_cast<vm_address_t>(output), &copied) == KERN_SUCCESS && copied == length;
#elif defined(__linux__)
    iovec local{output, length}, remote{reinterpret_cast<void*>(address), length};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(length);
#else
    (void)output;
    return false;
#endif
}

void Inspect(const TrapSnapshot& snapshot) {
    uint32_t instruction = 0;
    const bool read = ReadMemory(snapshot.pc, &instruction, sizeof(instruction));
    std::fprintf(stderr, "[KuDroidTrap] deferred-context signal=%d pc=0x%llx instruction-valid=%d word=0x%08x\n",
        snapshot.signal, static_cast<unsigned long long>(snapshot.pc), read, instruction);
    for (unsigned r = 0; r < 2; ++r) {
        char text[129]{};
        // Read bytewise so a short string at a page boundary remains inspectable.
        unsigned n = 0;
        for (; n < 128; ++n) {
            if (snapshot.x[r] > UINT64_MAX - n ||
                !ReadMemory(snapshot.x[r] + n, &text[n], 1)) break;
            if (text[n] == 0) {
                if (n != 0) std::fprintf(stderr, "[KuDroidTrap] deferred-message x%u=%s\n", r, text);
                break;
            }
            if (text[n] < 32 || text[n] > 126) break;
        }
    }
}
}

namespace kudroid {
bool record_trap_diagnostics(int signal, uint64_t pc, uint64_t lr, uint64_t sp,
                             const uint64_t (&registers)[8]) {
    const int saved = errno;
    SignalLine line;
    line.text("[KuDroidTrap] trap-context signal="); line.hex(static_cast<unsigned>(signal));
    line.text(" pc="); line.hex(pc);
    line.text(" lr="); line.hex(lr);
    line.text(" sp="); line.hex(sp);
    for (unsigned r = 0; r < 8; ++r) {
        line.text(" x");
        const char digit[2] = {static_cast<char>('0' + r), 0};
        line.text(digit); line.text("="); line.hex(registers[r]);
    }
    line.text("\n");
    (void)write(STDERR_FILENO, line.bytes, line.used);
    bool stored = false;
    for (auto& slot : g_slots) {
        unsigned empty = 0;
        if (!slot.state.compare_exchange_strong(empty, 1, std::memory_order_acquire)) continue;
        slot.snapshot.signal = signal;
        slot.snapshot.pc = pc;
        slot.snapshot.lr = lr;
        slot.snapshot.sp = sp;
        for (unsigned r = 0; r < 8; ++r) slot.snapshot.x[r] = registers[r];
        slot.state.store(2, std::memory_order_release);
        stored = true;
        break;
    }
    errno = saved;
    return stored;
}
}

extern "C" void kudroid_drain_trap_diagnostics(void) {
    const int saved = errno;
    for (auto& slot : g_slots) {
        unsigned ready = 2;
        if (!slot.state.compare_exchange_strong(ready, 3, std::memory_order_acquire)) continue;
        // The slot stays owned until inspection completes; nested traps use another slot.
        const auto snapshot = slot.snapshot;
        Inspect(snapshot);
        slot.state.store(0, std::memory_order_release);
    }
    errno = saved;
}
