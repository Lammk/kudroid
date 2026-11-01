#include "kudroid/ElfAliasStore.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

using namespace kudroid;
namespace {
int checks = 0, failures = 0;
void Check(bool ok, const char* message) {
    ++checks;
    failures += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
}
}

int main() {
    char path[] = "/tmp/kudroid-alias-store-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0 || ftruncate(fd, 32768) != 0) return 1;
    unlink(path);
    auto* write = static_cast<uint8_t*>(mmap(nullptr, 32768, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    auto* guest = static_cast<uint8_t*>(mmap(nullptr, 32768, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0));
    close(fd);
    if (write == MAP_FAILED || guest == MAP_FAILED) return 1;
    ElfWritableAliases aliases;
    const uintptr_t base = reinterpret_cast<uintptr_t>(guest);
    Check(aliases.Add(base, base + 0x3000, reinterpret_cast<uintptr_t>(write + 0x3000),
                      0x2000, base + 0x3000, 0x1000), "Register data sharing an RX page, excluding text and RELRO");
    Arm64StoreState state;
    state.x[20] = base + 0x3030;
    state.x[22] = 0x123456789abcdef0ULL;
    state.pc = base + 0x400;
    const auto before = state;
    Check(EmulateElfAliasStore(0xf9000296, state.x[20], state, aliases),
          "A real STR writes through the alias of an RX mapping");
    uint64_t observed = 0;
    std::memcpy(&observed, guest + 0x3030, 8);
    Check(observed == before.x[22], "Guest reads the committed state value through its original address");
    Check(std::memcmp(state.x, before.x, sizeof(state.x)) == 0 && state.pc == before.pc + 4,
          "A plain store preserves every source register and advances PC once");
    const uint32_t stores[] = {0x39000c20, 0x79000c20, 0xb9000c20, 0xf9000820,
        0xf81f8020, 0xf8008420, 0xf8008c20, 0xf822d820, 0xf8227820,
        0x3d800420, 0xa9010820, 0xa8810820, 0xa9bf0820, 0xad010820,
        0x6d010820, 0x2d010820, 0xa8010820};
    const int offsets[] = {3, 6, 12, 16, -8, 0, 8, -8, 8, 16, 16, 0, -16, 32, 16, 8, 16};
    const unsigned widths[] = {1, 2, 4, 8, 8, 8, 8, 8, 8, 16, 8, 8, 8, 16, 8, 4, 8};
    for (unsigned fixture = 0; fixture < sizeof(stores) / sizeof(stores[0]); ++fixture) {
        const uint32_t instruction = stores[fixture];
        Arm64StoreState registers;
        registers.x[0] = 0xfedcba9876543210ULL;
        registers.x[1] = base + 0x3100;
        registers.x[2] = uint64_t(-1);
        registers.pc = base + 0x400;
        for (unsigned n = 0; n < 16; ++n) {
            registers.v[0][n] = uint8_t(n + 1);
            registers.v[2][n] = uint8_t(n + 33);
        }
        const auto plan = DecodeArm64Store(instruction, registers);
        Check(plan.valid, "Assembler-verified ordinary store decodes");
        if (!plan.valid) continue;
        // Large unsigned index is replaced for the LSL fixture.
        if (instruction == 0xf8227820) {
            registers.x[2] = 1;
        }
        const auto decoded = DecodeArm64Store(instruction, registers);
        const auto original = registers;
        const uintptr_t address = base + 0x3100 + offsets[fixture];
        const bool vector = fixture == 9 || (fixture >= 13 && fixture <= 15);
        const bool pair = fixture >= 10;
        Check(decoded.address == address && decoded.bytes == widths[fixture] &&
              decoded.vector == vector && decoded.pair == pair, "Address and width match independent assembler fixtures");
        Check(EmulateElfAliasStore(instruction, address, registers, aliases),
              "Scalar, vector and pair stores commit with their addressing mode");
        auto* result = guest + (address - base);
        const void* expected = vector ? static_cast<const void*>(original.v[0])
                               : static_cast<const void*>(&original.x[0]);
        Check(std::memcmp(result, expected, widths[fixture]) == 0, "First store lane has exact bytes");
        if (pair) {
            expected = vector ? static_cast<const void*>(original.v[2])
                       : static_cast<const void*>(&original.x[2]);
            Check(std::memcmp(result + widths[fixture], expected, widths[fixture]) == 0,
                  "Pair store commits its second lane");
        }
        const int update = fixture == 5 || fixture == 6 ? 8 : fixture == 11 ? 16 : fixture == 12 ? -16 : 0;
        Check(registers.x[1] == original.x[1] + update &&
              registers.pc == original.pc + 4, "Pre/post-index update the original guest base exactly once");
    }
    state.x[31] = base + 0x3100;
    Check(EmulateElfAliasStore(0xf9000bff, base + 0x3110, state, aliases), "XZR stores zero while SP supplies the base");
    std::memcpy(&observed, guest + 0x3110, 8);
    Check(observed == 0, "XZR does not read the saved SP value as payload");
    state.x[31]++;
    Check(!DecodeArm64Store(0xf9000bff, state).valid, "Misaligned SP is not repaired");
    const uint32_t refused[] = {0xc89ffc20, 0xc8037c20, 0xf8208022, 0xf8200022,
        0x39800020, 0xf9800020, 0xd503201f, 0xf8008c21, 0xad410820};
    for (uint32_t instruction : refused) {
        Check(!DecodeArm64Store(instruction, state).valid,
              "Atomic, load, hint, or constrained-unpredictable instruction is refused");
    }
    state.x[20] = base + 0x2ff8;
    Check(!EmulateElfAliasStore(0xf9000296, state.x[20], state, aliases), "RELRO writes retain the real fault");
    state.x[20] = base + 0x4ffc;
    const auto refusedState = state;
    uint8_t bytes[8];
    std::memcpy(bytes, write + 0x4ffc, 8);
    Check(!EmulateElfAliasStore(0xf9000296, base + 0x4ffc, state, aliases) &&
          state.pc == refusedState.pc && std::memcmp(state.x, refusedState.x, sizeof(state.x)) == 0 &&
          std::memcmp(state.v, refusedState.v, sizeof(state.v)) == 0 &&
          std::memcmp(bytes, write + 0x4ffc, 8) == 0, "A partial out-of-range store changes no memory or registers");
    state.x[20] = base + 0x3ffc;
    Check(EmulateElfAliasStore(0xf9000296, base + 0x3ffc, state, aliases),
          "A supported unaligned store can cross from the shared page into ordinary data");
    state.x[20] = base + 0x4100;
    Check(!EmulateElfAliasStore(0xf9000296, state.x[20], state, aliases),
          "Permission faults on ordinary data pages are not swallowed");
    state.x[20] = base + 0x3100;
    Check(!EmulateElfAliasStore(0xf9000296, base + 0x3200, state, aliases), "Fault address must lie in the decoded transfer");
    Check(!aliases.Add(base, UINTPTR_MAX - 4, 0x1000, 8), "Overflowing guest ranges are rejected");
    aliases.Remove(base);
    Check(!aliases.Find(base + 0x3030, 8) && !aliases.ContainsFault(base + 0x3030), "Unloaded ranges are no longer eligible");
    Check(aliases.Add(base, base + 0x3000, reinterpret_cast<uintptr_t>(write + 0x3000), 0x1000) &&
          aliases.Add(base, base + 0x5000, reinterpret_cast<uintptr_t>(write + 0x5000), 0x1000),
          "Independent writable intervals register");
    aliases.RemoveOverlapping(base + 0x3800, 0x100);
    Check(!aliases.ContainsFault(base + 0x3030) && aliases.ContainsFault(base + 0x5000),
          "Permission removal disables overlapping aliases and preserves unrelated intervals");
    aliases.Remove(base);

    std::atomic<bool> done{false}, bad{false};
    std::thread reader([&] {
        while (!done.load()) {
            for (unsigned i = 0; i < 128; ++i) {
                void* found = aliases.Find(0x100000 + i * 16, 8);
                if (found && reinterpret_cast<uintptr_t>(found) != 0x200000 + i * 16) bad = true;
            }
        }
    });
    for (unsigned i = 0; i < 128; ++i) aliases.Add(0x1234, 0x100000 + i * 16, 0x200000 + i * 16, 8);
    aliases.Remove(0x1234);
    done = true;
    reader.join();
    Check(!bad, "Concurrent readers never observe partially published metadata");
    munmap(guest, 32768);
    munmap(write, 32768);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
