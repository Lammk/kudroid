#include "kudroid/elf_loader.hpp"

#include <chrono>
#include <cstdio>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    kudroid::ElfLoader loader(argv[1]);
    auto start = std::chrono::steady_clock::now();
    if (!loader.parse() || !loader.map()) {
        std::fprintf(stderr, "%s\n", loader.lastError());
        return 1;
    }
    auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    uint64_t hash = 14695981039346656037ull;
    for (const auto& segment : loader.segments()) {
        if (!(segment.flags & 1)) continue;
        auto* bytes = static_cast<const unsigned char*>(loader.baseAddress()) + segment.vaddr;
        for (uint64_t i = 0; i < segment.filesz; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    }
    std::printf("ELF map: %.3f ms, executable digest=%016llx\n", elapsed,
                static_cast<unsigned long long>(hash));
}
