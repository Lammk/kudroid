#!/usr/bin/env python3
"""Verify native descriptor callbacks and Java queue wakes share one looper."""

import argparse
from pathlib import Path
import subprocess
import tempfile


PREAMBLE = r'''
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
namespace kudroid { void frame_pacer_release_native_thread() {} }
enum class WaitKind { kLooperPoll };
struct BlockingWaitScope { BlockingWaitScope(WaitKind, void*, uintptr_t) {} };
void blocking_wait_note_budget(uint64_t) {}
uintptr_t guest_return_address(int) { return 0; }
extern "C" int64_t kudroid_looper_init();
extern "C" void kudroid_looper_poll(int64_t, int64_t);
extern "C" void kudroid_looper_wake(int64_t);
extern "C" void kudroid_looper_set_main(int64_t);
extern "C" void kudroid_looper_wake_main();
extern "C" void kudroid_looper_destroy(int64_t);
'''

CHECKS = r'''
struct CallbackState { int count = 0; std::thread::id owner; };
int callback(int fd, int events, void* data) {
    auto* state = static_cast<CallbackState*>(data);
    assert(state->owner == std::this_thread::get_id() && (events & 1));
    char byte; assert(::read(fd, &byte, 1) == 1);
    ++state->count;
    return 1;
}
int main() {
    const int64_t queue = kudroid_looper_init();
    assert(queue != 0);
    void* looper = bionic_ALooper_forThread();
    int fds[2]; assert(::pipe(fds) == 0);
    CallbackState state{0, std::this_thread::get_id()};
    assert(bionic_ALooper_addFd(looper, fds[0], -2, 1,
                               reinterpret_cast<void*>(&callback), &state) == 1);
    const char byte = 1; assert(::write(fds[1], &byte, 1) == 1);
    kudroid_looper_poll(queue, 0);
    assert(state.count == 1);
    puts("PASS: zero-time Java poll dispatches native callbacks on the owner thread");
    std::thread writer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(::write(fds[1], &byte, 1) == 1);
    });
    kudroid_looper_poll(queue, 500);
    writer.join(); assert(state.count == 2);
    puts("PASS: native descriptor readiness wakes a blocked Java queue");
    kudroid_looper_set_main(queue);
    std::thread waker([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        kudroid_looper_wake_main();
    });
    const auto begin = std::chrono::steady_clock::now();
    kudroid_looper_poll(queue, -1);
    waker.join();
    assert(std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(500));
    puts("PASS: touch/main-queue wake interrupts the shared native poll");
    kudroid_looper_wake(queue);
    const auto pending = std::chrono::steady_clock::now();
    kudroid_looper_poll(queue, -1);
    assert(std::chrono::steady_clock::now() - pending < std::chrono::milliseconds(100));
    puts("PASS: a wake before polling remains latched");
    assert(bionic_ALooper_removeFd(looper, fds[0]) == 1);
    kudroid_looper_destroy(queue);
    kudroid_looper_wake_main();
    ::close(fds[0]); ::close(fds[1]);
    puts("PASS: destroying the queue retires the touch wake target");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="Alternate NativeLooper.cpp for regression checks")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    shim = (root / "src/abi/SyscallShim.cpp").read_text()
    shim = shim[shim.index("#define ALOOPER_PREPARE_ALLOW_NON_CALLBACKS"):
                shim.index("// Bionic symbols game .so commonly import")]
    with tempfile.TemporaryDirectory(prefix="kudroid-native-looper-") as temporary:
        directory = Path(temporary)
        cpp = directory / "looper.cpp"
        cpp.write_text(PREAMBLE + shim + CHECKS)
        binary = directory / "looper-test"
        subprocess.run(["c++", "-std=c++17", "-pthread", str(cpp),
                        str(args.source or root / "src/platform/NativeLooper.cpp"),
                        "-o", str(binary)], check=True, timeout=120)
        subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    main()
