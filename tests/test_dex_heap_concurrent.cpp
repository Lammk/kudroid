#include "kudroid/kuart/DexHeap.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main() {
    kudroid::kuart::DexHeap heap;
    if (heap.Allocate(0) || heap.Allocate(SIZE_MAX)) return 1;
    constexpr size_t workers = 8, count = 12000, size = 64;
    std::vector<std::vector<void*>> allocations(workers);
    std::atomic<size_t> ready{0};
    std::vector<std::thread> threads;
    for (size_t worker = 0; worker < workers; ++worker) {
        allocations[worker].resize(count);
        threads.emplace_back([&, worker] {
            ready.fetch_add(1);
            while (ready.load() != workers) std::this_thread::yield();
            for (size_t i = 0; i < count; ++i) {
                void* memory = heap.Allocate(size);
                allocations[worker][i] = memory;
                if (memory) std::memset(memory, static_cast<int>(worker + 1), size);
            }
        });
    }
    for (auto& thread : threads) thread.join();
    std::vector<uintptr_t> addresses;
    size_t corrupt = 0;
    for (size_t worker = 0; worker < workers; ++worker) {
        for (void* memory : allocations[worker]) {
            if (!memory) { ++corrupt; continue; }
            addresses.push_back(reinterpret_cast<uintptr_t>(memory));
            for (size_t i = 0; i < size; ++i) {
                if (static_cast<unsigned char*>(memory)[i] != worker + 1) {
                    ++corrupt;
                    break;
                }
            }
        }
    }
    std::sort(addresses.begin(), addresses.end());
    size_t overlaps = 0;
    for (size_t i = 1; i < addresses.size(); ++i)
        overlaps += addresses[i] - addresses[i - 1] < size;
    const bool accounting = heap.BytesAllocated() == workers * count * size;
    std::printf("Concurrent heap: %zu allocations, %zu overlaps, %zu corrupt, accounting=%d\n",
                addresses.size(), overlaps, corrupt, accounting);
    return overlaps || corrupt || !accounting ? 1 : 0;
}
