// GuestLibrary.h — seam for the guest-library pipeline shared between the run
// entry (kudroid_bridge.cpp) and the diagnostic tests (DiagnosticTests.cpp).

#pragma once

#include <kudroid/elf_loader.hpp>

// Guest dlopen/dlsym over the LibraryManager, consumed by the
// kudroid_guest_library_* function pointers SyscallShim consults. Handles are
// ElfLoader pointers validated against an internal registry before
// dereference.
void* guestLibraryOpen(const char* filename);
void* guestLibrarySymbol(void* handle, const char* symbol);
int guestLibraryOwns(void* handle);

// Process-lifetime LibraryManager singleton. Guest .so mappings live for the
// whole process (unmapping under a detached thread aborts), so the manager is
// never destroyed.
kudroid::LibraryManager& globalLibraryManager();
