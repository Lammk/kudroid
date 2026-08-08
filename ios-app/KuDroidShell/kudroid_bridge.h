#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Self-test entry point for the kudroid_core library.
/// Returns 0 on success, non-zero on failure.
int kudroid_self_test(void);

/// Self-test with detailed log output.
/// Returns a malloc'd string containing step-by-step debug log.
/// Caller must free() the returned string.
const char* kudroid_self_test_log(void);

/// Load an ELF shared object (.so) file.
/// @param path  Absolute path to the .so file.
/// @return      A malloc'd log string with parse results.
///              Caller must free() the returned string.
const char* kudroid_load_elf(const char* path);

/// Execute a native function from the loaded .so (Phase 2 test).
/// Must be called after kudroid_load_elf.
/// @return  A malloc'd log string with execution result.
///          Caller must free() the returned string.
const char* kudroid_execution_test(const char* path);

/// Report whether JIT (executable memory) is available for this process.
/// @return  A malloc'd string "JIT: Enabled" or "JIT: Disabled".
///          Caller must free() the returned string.
const char* kudroid_jit_status(void);

#ifdef __cplusplus
}
#endif