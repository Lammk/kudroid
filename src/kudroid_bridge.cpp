#include "kudroid/elf_loader.hpp"

extern "C" int kudroid_self_test(void) {
    // Stub test: try to construct an ElfLoader (no file loaded yet).
    // Returns 0 if construction succeeds.
    try {
        kudroid::ElfLoader loader("/nonexistent");
        (void)loader;
        return 0;
    } catch (...) {
        return -1;
    }
}