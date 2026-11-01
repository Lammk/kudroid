#include "TrapDiagnostics.h"
#include "kudroid/kudroid_bridge.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <thread>
#include <sys/mman.h>
#include <unistd.h>

namespace {
int checks = 0, failures = 0;
uint64_t g_registers[8]{};
uint64_t g_pc = 0;
volatile sig_atomic_t g_recorded = 0;

void Check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", message); }
}
void CaptureSignal(int signal) {
    g_recorded = kudroid::record_trap_diagnostics(signal, g_pc, 0x1234, 0x5678, g_registers);
}
}

int main() {
    char path[] = "/tmp/kudroid-trap-diagnostics-XXXXXX";
    int file = mkstemp(path);
    if (file < 0) return 1;
    unlink(path);
    const int previous = dup(STDERR_FILENO);
    if (previous < 0 || dup2(file, STDERR_FILENO) < 0) return 1;
    const long page = sysconf(_SC_PAGESIZE);
    auto* memory = static_cast<unsigned char*>(mmap(nullptr, page * 2,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (memory == MAP_FAILED) return 1;
    const uint32_t instruction = 0xd4200000;
    std::memcpy(memory, &instruction, sizeof(instruction));
    const char message[] = "bounded assertion";
    auto* atBoundary = memory + page - sizeof(message);
    std::memcpy(atBoundary, message, sizeof(message));
    Check(mprotect(memory + page, page, PROT_NONE) == 0, "guard page created");
    g_pc = reinterpret_cast<uint64_t>(memory);
    g_registers[0] = reinterpret_cast<uint64_t>(atBoundary);
    g_registers[1] = reinterpret_cast<uint64_t>(memory + page);
    struct sigaction action{}, old{};
    action.sa_handler = CaptureSignal;
    sigemptyset(&action.sa_mask);
    Check(sigaction(SIGUSR1, &action, &old) == 0, "test handler installed");
    errno = EDOM;
    Check(raise(SIGUSR1) == 0 && g_recorded == 1, "signal capture queues scalar state");
    Check(errno == EDOM, "signal capture preserves errno");
    kudroid_drain_trap_diagnostics();
    Check(errno == EDOM, "deferred inspection preserves errno");
    sigaction(SIGUSR1, &old, nullptr);

    g_registers[0] = UINT64_MAX;
    g_registers[1] = 1;
    for (unsigned i = 0; i < 4; ++i)
        Check(kudroid::record_trap_diagnostics(SIGTRAP, UINT64_MAX, i, i, g_registers), "free slot accepts capture");
    Check(!kudroid::record_trap_diagnostics(SIGTRAP, 1, 0, 0, g_registers), "full queue refuses overwrite");
    std::thread first([] { kudroid_drain_trap_diagnostics(); });
    std::thread second([] { kudroid_drain_trap_diagnostics(); });
    first.join(); second.join();
    Check(kudroid::record_trap_diagnostics(SIGILL, 1, 0, 0, g_registers), "drained slots are reusable");
    kudroid_drain_trap_diagnostics();

    std::fflush(stderr);
    dup2(previous, STDERR_FILENO);
    close(previous);
    lseek(file, 0, SEEK_SET);
    std::string output;
    char bytes[1024];
    ssize_t n;
    while ((n = read(file, bytes, sizeof(bytes))) > 0) output.append(bytes, n);
    close(file);
    Check(output.find("pc=0x") != std::string::npos && output.find("x7=0x") != std::string::npos,
          "immediate scalar context was written");
    Check(output.find("instruction-valid=1 word=0xd4200000") != std::string::npos,
          "instruction inspected on normal thread");
    Check(output.find("deferred-message x0=bounded assertion") != std::string::npos,
          "short boundary string inspected without touching guard page");
    Check(output.find("instruction-valid=0") != std::string::npos, "invalid pointers do not fault");
    unsigned count = 0;
    for (size_t pos = 0; (pos = output.find("deferred-context", pos)) != std::string::npos; ++pos) ++count;
    Check(count == 6, "concurrent drains inspect each queued snapshot once");
    munmap(memory, page * 2);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
