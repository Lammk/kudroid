#!/usr/bin/env python3
"""Exercise the alternate-stack translator with real host signal delivery."""

import argparse
from pathlib import Path
import subprocess
import tempfile


PREAMBLE = r'''
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
// Exercise the Darwin-only translation branch on the host kernel.
#ifndef __APPLE__
#define __APPLE__ 1
#endif
namespace kudroid {
'''

CHECKS = r'''
}
using kudroid::GuestStack;
using kudroid::guest_sigaltstack;
static int failures = 0;
static volatile uintptr_t handler_address = 0;
static volatile sig_atomic_t handler_replace_result = 0;
static volatile sig_atomic_t handler_replace_errno = 0;
static bool replace_in_handler = false;
static GuestStack replacement{};
void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
void handle(int) {
    volatile unsigned char local[512];
    for (unsigned i = 0; i < sizeof(local); ++i) local[i] = 0x5a;
    handler_address = reinterpret_cast<uintptr_t>(&local[0]);
    if (replace_in_handler) {
        handler_replace_result = guest_sigaltstack(&replacement, nullptr);
        handler_replace_errno = errno;
    }
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    constexpr size_t capacity = 64 * 1024;
    auto* memory = static_cast<unsigned char*>(::mmap(
        nullptr, capacity * 3, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (memory == MAP_FAILED) return 2;
    stack_t saved{};
    if (::sigaltstack(nullptr, &saved) != 0) return 2;
    stack_t host_private{};
    host_private.ss_sp = memory + capacity * 2;
    host_private.ss_size = capacity;
    if (::sigaltstack(&host_private, nullptr) != 0) return 2;
    struct sigaction action{}, old_action{};
    action.sa_handler = handle;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    if (::sigaction(SIGUSR1, &action, &old_action) != 0) return 2;
    GuestStack request{memory, 0, 0, 16 * 1024};
    GuestStack previous{}, queried{};
    replacement = GuestStack{memory + capacity, 0, 0, capacity};
    if (std::strcmp(argv[1], "delivery") == 0) {
        std::memset(memory + request.size, 0xa5, capacity - request.size);
        check(guest_sigaltstack(&request, nullptr) == 0, "install supplied stack");
        stack_t host{};
        ::sigaltstack(nullptr, &host);
        check(host.ss_sp == request.sp && host.ss_size == request.size,
              "kernel uses exactly the caller's allocation");
        ::raise(SIGUSR1);
        check(handler_address >= reinterpret_cast<uintptr_t>(request.sp) &&
              handler_address < reinterpret_cast<uintptr_t>(request.sp) + request.size,
              "signal handler executes inside supplied buffer");
        bool intact = true;
        for (size_t i = request.size; i < capacity; ++i) intact &= memory[i] == 0xa5;
        check(intact, "signal delivery leaves adjacent allocations untouched");
    } else if (std::strcmp(argv[1], "previous") == 0) {
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.flags == 2 &&
              queried.sp == nullptr && queried.size == 0, "host stack stays hidden");
        check(guest_sigaltstack(&request, &previous) == 0 && previous.flags == 2 &&
              previous.sp == nullptr && previous.size == 0,
              "first installation returns the previous disabled guest state");
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.flags == 0 &&
              queried.sp == request.sp && queried.size == request.size,
              "query reports installed pointer and size");
        check(guest_sigaltstack(&replacement, &previous) == 0 && previous.flags == 0 &&
              previous.sp == request.sp && previous.size == request.size,
              "replacement returns previous stack");
        GuestStack disabled{nullptr, 2, 0, 0};
        check(guest_sigaltstack(&disabled, &previous) == 0 && previous.flags == 0 &&
              previous.sp == replacement.sp && previous.size == replacement.size,
              "disable returns the stack that was replaced");
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.flags == 2,
              "disabled state is visible after the call");
        GuestStack aliased = request;
        check(guest_sigaltstack(&aliased, &aliased) == 0 && aliased.flags == 2 &&
              aliased.sp == nullptr && aliased.size == 0,
              "input and output may share storage");
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.sp == request.sp &&
              queried.size == request.size, "aliased call still installs input stack");
    } else if (std::strcmp(argv[1], "failure") == 0) {
        GuestStack tiny{memory, 0, 0, 1};
        errno = 0;
        check(guest_sigaltstack(&tiny, nullptr) == -1 && errno == ENOMEM,
              "undersized stack preserves host ENOMEM");
        stack_t host{};
        ::sigaltstack(nullptr, &host);
        check(host.ss_sp == host_private.ss_sp && host.ss_size == host_private.ss_size,
              "failed installation leaves host stack intact");
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.flags == 2,
              "failed installation leaves guest state disabled");
        check(guest_sigaltstack(&request, nullptr) == 0, "install before active-stack check");
        replace_in_handler = true;
        ::raise(SIGUSR1);
        check(handler_replace_result == -1 && handler_replace_errno == EPERM,
              "active stack replacement preserves host EPERM");
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.sp == request.sp &&
              queried.size == request.size, "active-stack failure preserves guest state");
        errno = 0;
        check(guest_sigaltstack(reinterpret_cast<void*>(0x4e), nullptr) == -1 &&
              errno == EFAULT, "low input pointer returns EFAULT");
        errno = 0;
        check(guest_sigaltstack(nullptr, reinterpret_cast<void*>(0x4e)) == -1 &&
              errno == EFAULT, "low output pointer returns EFAULT");
    } else if (std::strcmp(argv[1], "thread") == 0) {
        check(guest_sigaltstack(&request, nullptr) == 0, "install owner-thread stack");
        bool isolated = false;
        std::thread other([&] {
            GuestStack state{};
            isolated = guest_sigaltstack(nullptr, &state) == 0 && state.flags == 2;
        });
        other.join();
        check(isolated, "guest stack state is thread-local");
        check(guest_sigaltstack(nullptr, &queried) == 0 && queried.sp == request.sp &&
              queried.size == request.size, "other thread leaves owner stack unchanged");
    } else return 2;
    ::sigaction(SIGUSR1, &old_action, nullptr);
    ::sigaltstack(&saved, nullptr);
    ::munmap(memory, capacity * 3);
    return failures ? 1 : 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Alternate GuestSignals.cpp for regression checks")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (args.source or root / "src/abi/GuestSignals.cpp").read_text()
    layout = source[source.index("struct GuestStack {"):
                    source.index("// Linux arm64 struct sigcontext")]
    function = source[source.index("int guest_sigaltstack("):
                      source.index("void guest_signal_reset_for_test()")]
    with tempfile.TemporaryDirectory(prefix="kudroid-altstack-") as temporary:
        directory = Path(temporary)
        cpp = directory / "altstack.cpp"
        cpp.write_text(PREAMBLE + layout + function + CHECKS)
        binary = directory / "altstack-test"
        subprocess.run(["c++", "-std=c++17", "-pthread", "-Wall", "-Wextra",
                        str(cpp), "-o", str(binary)], check=True, timeout=120)
        failed = False
        for case in ("delivery", "previous", "failure", "thread"):
            result = subprocess.run([str(binary), case], timeout=10)
            failed |= result.returncode != 0
        raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
