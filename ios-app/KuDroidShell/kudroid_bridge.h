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

/// Load the Bionic shim test library and execute kudroid_bionic_test().
/// Returns a malloc'd diagnostic log; caller must free() it.
const char* kudroid_bionic_execution_test(const char* path);

/// Load bundled ELF libraries through LibraryManager and test global resolution.
/// Returns a malloc'd diagnostic log; caller must free() it.
const char* kudroid_multi_elf_test(const char* consumerPath, const char* providerPath);

/// Report whether JIT (executable memory) is available for this process.
/// @return  A malloc'd string "JIT: Enabled" or "JIT: Disabled".
///          Caller must free() the returned string.
const char* kudroid_jit_status(void);

/// Set the directory where kudroid_core writes .txt logs and crash dumps.
/// Call once at startup with the app's writable Documents directory.
/// Also installs signal handlers so a native crash still leaves a log file.
void kudroid_set_log_dir(const char* dir);

/// Set the Documents directory used by VFSPathRemapper.
void kudroid_set_documents_dir(const char* dir);

/// Set the CAMetalLayer or UIView pointer used for ANativeWindow surface bindings.
void kudroid_set_metal_layer(void* layer);

/// Run the VFS redirect and I/O self-test; returns a malloc'd log.
const char* kudroid_vfs_self_test_log(void);
const char* kudroid_vfs_extended_test_log(void);

/// Test the JVM integration.
/// Returns a malloc'd diagnostic log; caller must free() it.
char* kudroid_test_jvm(void);
char* kudroid_test_gpu(void);

/// Load a GPU test ARM64 .so file and execute its Vulkan test via BionicShim intercept.
/// Returns a malloc'd diagnostic log; caller must free() it.
const char* kudroid_gpu_vulkan_so_test(const char* path);

/// Load a GPU test ARM64 .so file and execute its OpenGL+EGL test via BionicShim intercept.
/// Returns a malloc'd diagnostic log; caller must free() it.
const char* kudroid_gpu_opengl_so_test(const char* path);

/// Extract and install an APK's arm64-v8a native libraries into android_root.
/// Returns a malloc'd diagnostic log; caller must free() it.
const char* kudroid_install_apk(const char* apkPath);

/// Scan the installed APK's library directory and load all its native libraries (.so).
/// Returns a malloc'd diagnostic log; caller must free() it.
const char* kudroid_run_apk(const char* appName);

/// Clear an application's internal cache directories.
/// Returns 1 on success, 0 on failure.
int kudroid_clear_app_cache(const char* package_name);

/// Completely delete an installed application and its data.
/// Returns 1 on success, 0 on failure.
int kudroid_delete_app(const char* package_name);

/// Get basic info about an installed application.
/// Returns a malloc'd string (e.g., JSON or formatted text); caller must free() it.
const char* kudroid_get_app_info(const char* package_name);

#ifdef __cplusplus
}
#endif