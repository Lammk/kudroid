#include "kudroid/kuart/MethodResolutionTrace.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <unistd.h>

namespace kudroid::kuart {
namespace {
constexpr size_t kEntries = 64;
constexpr size_t kLineBytes = 384;
struct Entry {
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<uint64_t> sequence{0};
    std::atomic<uintptr_t> thread{0};
    std::array<std::atomic<char>, kLineBytes> text{};
};
static_assert(std::atomic<char>::is_always_lock_free);
static_assert(std::atomic<uintptr_t>::is_always_lock_free);
static_assert(std::atomic<uint64_t>::is_always_lock_free);
std::array<Entry, kEntries> entries{};
std::atomic<uint64_t> next{1};
}

void RecordMethodResolution(uintptr_t thread, const char* text) {
    if (!thread || !text) return;
    const uint64_t sequence = next.fetch_add(1, std::memory_order_relaxed);
    Entry& entry = entries[sequence % kEntries];
    if (entry.writing.test_and_set(std::memory_order_acquire)) return;
    entry.sequence.store(0, std::memory_order_release);
    entry.thread.store(thread, std::memory_order_relaxed);
    size_t i = 0;
    for (; i + 2 < kLineBytes && text[i] && text[i] != '\n'; ++i)
        entry.text[i].store(text[i], std::memory_order_relaxed);
    entry.text[i++].store('\n', std::memory_order_relaxed);
    entry.text[i].store(0, std::memory_order_relaxed);
    entry.sequence.store(sequence, std::memory_order_release);
    entry.writing.clear(std::memory_order_release);
}

void WriteMethodResolutionTrace(int fd, uintptr_t thread) {
    const int savedErrno = errno;
    const uint64_t end = next.load(std::memory_order_acquire);
    const uint64_t start = end > kEntries ? end - kEntries : 1;
    const char header[] = "\n--- method resolution history ---\n";
    (void)!write(fd, header, sizeof(header) - 1);
    for (uint64_t sequence = start; sequence < end; ++sequence) {
        Entry& entry = entries[sequence % kEntries];
        // Skip an interrupted writer instead of waiting in the signal handler.
        if (entry.writing.test_and_set(std::memory_order_acquire)) continue;
        if (entry.sequence.load(std::memory_order_relaxed) != sequence ||
            entry.thread.load(std::memory_order_relaxed) != thread) {
            entry.writing.clear(std::memory_order_release);
            continue;
        }
        char line[kLineBytes];
        size_t length = 0;
        for (; length < sizeof(line); ++length) {
            line[length] = entry.text[length].load(std::memory_order_relaxed);
            if (!line[length]) break;
        }
        (void)!write(fd, line, length);
        entry.writing.clear(std::memory_order_release);
    }
    errno = savedErrno;
}
}
