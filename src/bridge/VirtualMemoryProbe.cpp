#include "kudroid/kudroid_bridge.h"
#include "kudroid/RuntimeCageAdapter.h"

#include <cerrno>
#include <cstdio>
#include <cstdint>

#if defined(__APPLE__)
#include "kudroid/MachVmDecls.h"
#include <mach/task_info.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr mach_vm_size_t kProbeLength = 8ULL << 30;

struct VmState {
    task_vm_info_data_t info{};
    kern_return_t infoResult = KERN_FAILURE;
    mach_msg_type_number_t infoCount = 0;
    mach_vm_address_t gapBase = 0;
    mach_vm_size_t gapSize = 0;
    unsigned regions = 0;
    bool complete = false;
};

struct ProbeResult {
    VmState state;
    mach_vm_address_t address = 0;
    kern_return_t allocation = KERN_FAILURE;
    kern_return_t protectNone = KERN_FAILURE;
    kern_return_t commit = KERN_FAILURE;
    kern_return_t decommit = KERN_FAILURE;
    kern_return_t release = KERN_FAILURE;
    bool spanVerified = false;
    unsigned tailValue = 0;
};

constexpr ProbeResult kEmptyProbe{};
ProbeResult g_earlyResult = kEmptyProbe;

uint64_t MonotonicNs() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + now.tv_nsec;
}

VmState ReadVmState() {
    VmState state;
    state.infoCount = TASK_VM_INFO_COUNT;
    state.infoResult = task_info(mach_task_self(), TASK_VM_INFO,
        reinterpret_cast<task_info_t>(&state.info), &state.infoCount);
    return state;
}

void ScanGaps(VmState& state) {
    if (state.infoResult != KERN_SUCCESS || state.infoCount < TASK_VM_INFO_REV2_COUNT)
        return;
    const auto minimum = state.info.min_address;
    const auto maximum = state.info.max_address;
    if (minimum >= maximum) return;
    mach_vm_address_t cursor = minimum;
    const uint64_t started = MonotonicNs();
    auto noteGap = [&](mach_vm_address_t start, mach_vm_address_t end) {
        if (end > start && end - start > state.gapSize) {
            state.gapBase = start;
            state.gapSize = end - start;
        }
    };
    for (unsigned i = 0; i < 262144 && cursor < maximum; ++i) {
        // Bound diagnostic work even if the VM map changes during traversal.
        if ((i & 255) == 0 && MonotonicNs() - started > 1000000000ULL) return;
        mach_vm_address_t address = cursor;
        mach_vm_size_t size = 0;
        kudroid_vm_region_basic_info_64_t info{};
        mach_msg_type_number_t count = KUDROID_VM_REGION_BASIC_INFO_64_COUNT;
        mach_port_t object = MACH_PORT_NULL;
        const kern_return_t result = mach_vm_region(mach_task_self(), &address, &size,
            KUDROID_VM_REGION_BASIC_INFO_64_FLAVOR,
            reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (result == KERN_INVALID_ADDRESS || (result == KERN_SUCCESS && address >= maximum)) {
            noteGap(cursor, maximum);
            state.complete = true;
            return;
        }
        if (result != KERN_SUCCESS || size == 0 || address > UINT64_MAX - size)
            return;
        if (address > cursor) noteGap(cursor, address);
        const auto end = address + size;
        if (end <= cursor) return;
        cursor = end < maximum ? end : maximum;
        ++state.regions;
    }
    state.complete = cursor == maximum;
}

bool VerifySpan(mach_vm_address_t address, mach_vm_size_t length) {
    if (address > UINT64_MAX - length) return false;
    const auto end = address + length;
    auto cursor = address;
    for (unsigned i = 0; i < 262144 && cursor < end; ++i) {
        mach_vm_address_t base = cursor;
        mach_vm_size_t size = 0;
        kudroid_vm_region_basic_info_64_t info{};
        mach_msg_type_number_t count = KUDROID_VM_REGION_BASIC_INFO_64_COUNT;
        mach_port_t object = MACH_PORT_NULL;
        const auto result = mach_vm_region(mach_task_self(), &base, &size,
            KUDROID_VM_REGION_BASIC_INFO_64_FLAVOR,
            reinterpret_cast<vm_region_info_t>(&info), &count, &object);
        if (object != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), object);
        if (result != KERN_SUCCESS || base > cursor || size == 0 ||
            base > UINT64_MAX - size || base + size <= cursor ||
            info.protection != VM_PROT_NONE ||
            (info.max_protection & VM_PROT_DEFAULT) != VM_PROT_DEFAULT) return false;
        cursor = base + size;
    }
    return cursor >= end;
}

ProbeResult RunProbe() {
    ProbeResult result;
    result.state = ReadVmState();
    result.allocation = mach_vm_allocate(mach_task_self(), &result.address,
                                         kProbeLength, VM_FLAGS_ANYWHERE);
    if (result.allocation == KERN_SUCCESS) {
        result.protectNone = mach_vm_protect(mach_task_self(), result.address,
                                             kProbeLength, FALSE, VM_PROT_NONE);
        if (result.protectNone == KERN_SUCCESS) {
            result.spanVerified = VerifySpan(result.address, kProbeLength);
            const long page = sysconf(_SC_PAGESIZE);
            if (result.spanVerified && page > 0 && static_cast<uint64_t>(page) <= kProbeLength) {
                const auto tail = result.address + kProbeLength - page;
                result.commit = mach_vm_protect(mach_task_self(), tail, page,
                                                FALSE, VM_PROT_DEFAULT);
                if (result.commit == KERN_SUCCESS) {
                    auto* byte = reinterpret_cast<volatile unsigned char*>(tail);
                    *byte = 0x57;
                    result.tailValue = *byte;
                    result.decommit = mach_vm_protect(mach_task_self(), tail, page,
                                                      FALSE, VM_PROT_NONE);
                }
            }
        }
        result.release = mach_vm_deallocate(mach_task_self(), result.address, kProbeLength);
    }
    // An apparent gap is diagnostic evidence, not permission to map there.
    ScanGaps(result.state);
    return result;
}

void LogState(const char* phase, const VmState& state) {
    const bool bounds = state.infoResult == KERN_SUCCESS && state.infoCount >= TASK_VM_INFO_REV2_COUNT;
    std::fprintf(stderr,
        "[KuDroidVmProbe] phase=%s pid=%d task-info=%d bounds-valid=%d "
        "min=0x%llx max=0x%llx virtual=%llu declared-regions=%d scanned=%u complete=%d "
        "largest-gap-base=0x%llx largest-gap-size=%llu\n",
        phase, getpid(), state.infoResult, bounds,
        static_cast<unsigned long long>(bounds ? state.info.min_address : 0),
        static_cast<unsigned long long>(bounds ? state.info.max_address : 0),
        static_cast<unsigned long long>(state.info.virtual_size), state.info.region_count,
        state.regions, state.complete,
        static_cast<unsigned long long>(state.gapBase),
        static_cast<unsigned long long>(state.gapSize));
}

void LogProbe(const char* phase, const ProbeResult& result) {
    LogState(phase, result.state);
    std::fprintf(stderr,
        "[KuDroidVmProbe] phase=%s length=%llu allocate=%d base=0x%llx "
        "protect-none=%d full-span=%d tail-commit=%d tail-value=%u tail-decommit=%d release=%d\n",
        phase, static_cast<unsigned long long>(kProbeLength), result.allocation,
        static_cast<unsigned long long>(result.address), result.protectNone,
        result.spanVerified, result.commit, result.tailValue, result.decommit, result.release);
}

// Capture before host services start; logging is initialized later.
__attribute__((constructor)) void CaptureEarlyReservation() {
    const int saved = errno;
    g_earlyResult = RunProbe();
    kudroid::prepareRuntimeCageReservations();
    errno = saved;
}
}
#endif

extern "C" void kudroid_log_vm_startup_probe(void) {
#if defined(__APPLE__)
    const int saved = errno;
    LogProbe("native-initializer", g_earlyResult);
    kudroid::logEarlyRuntimeCageReservations();
    errno = saved;
#endif
}

extern "C" void kudroid_probe_virtual_memory(const char* phase) {
#if defined(__APPLE__)
    const int saved = errno;
    LogProbe(phase ? phase : "manual", RunProbe());
    errno = saved;
#else
    (void)phase;
#endif
}

extern "C" void kudroid_log_virtual_memory_state(const char* phase) {
#if defined(__APPLE__)
    const int saved = errno;
    auto state = ReadVmState();
    ScanGaps(state);
    LogState(phase ? phase : "manual", state);
    errno = saved;
#else
    (void)phase;
#endif
}
