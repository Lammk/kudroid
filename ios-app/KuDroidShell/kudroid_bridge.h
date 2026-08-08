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

#ifdef __cplusplus
}
#endif