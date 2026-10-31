#!/usr/bin/env python3
"""Compare the GCD timer shim's deadline behavior with Linux timerfd."""

import argparse
from pathlib import Path
import subprocess
import tempfile


PREAMBLE = r'''
#include "kudroid/abi/TimerDeadline.h"
#include <dispatch/dispatch.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <map>
#include <set>
#include <mutex>
#include <cassert>
#include <cstdio>
extern "C" int bionic_clock_gettime(int clock, timespec* out) {
    return ::clock_gettime(clock, out);
}
'''

CHECKS = r'''
int main() {
    const auto create = [](int clock) {
        return SHIM ? bionic_timerfd_create(clock, TFD_NONBLOCK | TFD_CLOEXEC)
                    : ::timerfd_create(clock, TFD_NONBLOCK | TFD_CLOEXEC);
    };
    const auto arm = [](int fd, int flags, timespec value) {
        bionic_itimerspec spec{};
        spec.it_value = value;
        return SHIM ? bionic_timerfd_settime(fd, flags, &spec, nullptr)
                    : ::timerfd_settime(fd, flags,
                          reinterpret_cast<itimerspec*>(&spec), nullptr);
    };
    const auto ready = [](int fd, int ms) {
        pollfd p{fd, POLLIN, 0};
        return ::poll(&p, 1, ms) == 1 && (p.revents & POLLIN);
    };
    const auto drain = [](int fd) {
        uint64_t count = 0;
        assert(::read(fd, &count, sizeof(count)) == sizeof(count) && count > 0);
    };
    int fd = create(CLOCK_MONOTONIC);
    assert(fd >= 0);
    assert(arm(fd, TFD_TIMER_ABSTIME, {1, 0}) == 0);
    assert(ready(fd, 500)); drain(fd);
    puts("PASS: past absolute deadline expires immediately");
    timespec now{}; ::clock_gettime(CLOCK_MONOTONIC, &now);
    now.tv_nsec += 80000000;
    if (now.tv_nsec >= 1000000000) { ++now.tv_sec; now.tv_nsec -= 1000000000; }
    assert(arm(fd, TFD_TIMER_ABSTIME, now) == 0);
    assert(!ready(fd, 10)); assert(ready(fd, 500)); drain(fd);
    puts("PASS: future absolute deadline uses the timer clock");
    assert(arm(fd, 0, {0, 80000000}) == 0);
    assert(!ready(fd, 10)); assert(ready(fd, 500)); drain(fd);
    puts("PASS: relative deadline remains relative");
    assert(arm(fd, 0, {0, 80000000}) == 0);
    assert(arm(fd, TFD_TIMER_ABSTIME, {0, 0}) == 0);
    assert(!ready(fd, 120));
    puts("PASS: zero absolute deadline disarms");
    assert(arm(fd, 0, {20, 0}) == 0);
    assert(arm(fd, TFD_TIMER_ABSTIME, {1, 0}) == 0);
    assert(ready(fd, 500)); drain(fd);
    puts("PASS: rearming replaces the previous deadline");
    assert(arm(fd, 0, {0, 80000000}) == 0);
    assert(arm(fd, 0, {0, 1000000000}) == -1 && errno == EINVAL);
    assert(ready(fd, 500)); drain(fd);
    puts("PASS: invalid nanoseconds leave the armed timer intact");
    fd = create(CLOCK_REALTIME);
    assert(fd >= 0 && arm(fd, TFD_TIMER_ABSTIME, {1, 0}) == 0);
    assert(ready(fd, 500)); drain(fd);
    puts("PASS: realtime absolute deadline uses realtime");
    assert(kudroid::timer_deadline_delay_ns({2, 1}, {1, 999999999}, true) == 2);
    assert(kudroid::timer_deadline_delay_ns({INT64_MAX, 999999999}, {}, false) == INT64_MAX);
    puts("PASS: nanosecond borrow and large duration saturation");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--swift-root", type=Path, required=True,
                        help="Swift toolchain usr directory containing lib/swift/linux/libdispatch.so")
    parser.add_argument("--source", type=Path,
                        help="Alternate SyscallShim.cpp for a negative regression run")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (args.source or root / "src/abi/SyscallShim.cpp").read_text()
    source = source[source.index("static int create_loopback_udp()"):
                    source.index('extern "C" int bionic_epoll_create(int size)')]
    selected, apple = [], False
    for line in source.splitlines():
        if line == "#ifdef __APPLE__":
            apple = True
        elif line == "#else":
            apple = False
        elif line == "#endif":
            apple = True
        elif apple:
            selected.append(line)
    # The first extracted block already starts inside the Apple branch.
    prefix = source[:source.index("#endif")]
    selected = prefix.splitlines() + selected
    swift = args.swift_root / "lib/swift"
    with tempfile.TemporaryDirectory(prefix="kudroid-timerfd-") as temporary:
        directory = Path(temporary)
        cpp = directory / "timers.cpp"
        cpp.write_text(PREAMBLE + "\n".join(selected) + CHECKS)
        for shim in (0, 1):
            binary = directory / f"timer-{shim}"
            subprocess.run([str(args.swift_root / "bin/clang++"), "-std=c++17", "-fblocks",
                            f"-DSHIM={shim}", "-I", str(root / "include"), "-I", str(swift),
                            str(cpp), "-L", str(swift / "linux"), "-ldispatch", "-lBlocksRuntime",
                            "-pthread", f"-Wl,-rpath,{swift / 'linux'}", "-o", str(binary)],
                           check=True, timeout=120)
            print("Linux timerfd" if not shim else "GCD timer shim", flush=True)
            subprocess.run([str(binary)], check=True, timeout=15)


if __name__ == "__main__":
    main()
