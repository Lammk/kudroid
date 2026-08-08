#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Self-test entry point for the kudroid_core library.
/// Returns 0 on success, non-zero on failure.
int kudroid_self_test(void);

#ifdef __cplusplus
}
#endif