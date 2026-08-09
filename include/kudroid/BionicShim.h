#pragma once

#include <cstddef>
#include <cstdint>

namespace kudroid {

/// Resolve an Android/Bionic symbol to an iOS/POSIX-compatible implementation.
/// Unknown symbols return a non-null dummy function and emit a warning.
void* resolve_bionic_symbol(const char* name);

/// Clear and retrieve diagnostic messages produced by the shim.
void bionic_shim_reset_trace();
const char* bionic_shim_trace();

/// Example use from an ELF relocation/symbol-binding loop:
///
///     void* address = resolve_bionic_symbol(symbolName);
///     *relocationTarget = reinterpret_cast<std::uintptr_t>(address);
///
/// For symbols implemented by the loaded ELF itself, try getSymbolAddress()
/// first; use this resolver for imported Bionic/liblog symbols.

} // namespace kudroid
