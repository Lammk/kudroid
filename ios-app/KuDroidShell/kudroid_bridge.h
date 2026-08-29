#pragma once

#ifdef __cplusplus
extern "C" {
#endif

///self-test entry point for kudroid_core library.
/// returns 0 on success, non-zero on failure.
int kudroid_self_test(void);

///self-test with detailed log output.
/// returns a malloc string containing the step-by-step debug log.
/// the caller must free the string returned by free().
const char* kudroid_self_test_log(void);

/// loads an elf shared object (.so) file.
/// @param path absolute path to the .so file.
/// @return the malloc log string containing the analysis results.
/// the caller must free the string returned by free().
const char* kudroid_load_elf(const char* path);

/// executes a native function from the loaded .so file (phase 2 test).
/// must be called after kudroid_load_elf.
/// @return the malloc log string containing the execution results.
/// the caller must free the string returned by free().
const char* kudroid_execution_test(const char* path);

/// executes a native function from the loaded .so file (phase 2 test).
/// must be called after kudroid_load_elf.
/// @return the malloc log string containing the execution results.
/// the caller must free the string returned by free().
const char* kudroid_syscall_so_test(const char* path);
const char* kudroid_jni_massive_so_test(const char* path);

///load the bionic shim test library and execute kudroid_bionic_test().
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_bionic_execution_test(const char* path);

///load packaged elf libraries via librarymanager and check global resolution.
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_multi_elf_test(const char* consumerPath, const char* providerPath);

/// reports whether jit (executable memory) is available to this process.
/// @return a malloc string "jit: enabled" or "jit: disabled".
/// the caller must free the string returned by free().
const char* kudroid_jit_status(void);

/// sets the directory where kudroid_core writes .txt log files and crash dumps.
/// called once at startup against the application's writable documents directory.
/// also installs signal handlers so that an original crash still leaves a log file.
void kudroid_set_log_dir(const char* dir);

/// sets the documents directory used by vfspathremapper.
void kudroid_set_documents_dir(const char* dir);

///set the cametallayer or uiview cursor used for anativewindow surface constraints.
///width/height is the actual pixel size (UIScreen.bounds * scale); density = scale
/// (3.0 for @3x) — pushed to Java's DisplayMetrics at JVM initialization.
void kudroid_set_metal_layer(void* layer, int width, int height, float density);
void kudroid_unbind_metal_layer(void);

/// set and get required screen orientation from Activity (0=Landscape, 1=Portrait, -1=Unspecified)
void kudroid_set_requested_orientation(int orientation);
int kudroid_get_requested_orientation(void);

///transfer sensor data (Accelerometer/Gyroscope/Orientation) from iOS CoreMotion to guest app
void kudroid_inject_sensor_event(int sensorType, float x, float y, float z);

/// activate Haptic Feedback vibration (1=Light, 2=Moderate, 3=Strong)
void kudroid_vibrate(int intensity);

/// manage the keep screen on flag (1=Keep Screen On / No Sleep, 0=Allow Sleep)
void kudroid_set_keep_screen_on(int keepOn);
int kudroid_get_keep_screen_on(void);

///insert a touch event in the native android app
/// @param x x coordinate of the touch
/// @param y y coordinate of the touch
/// @param action 0=down, 1=up, 2=move (mapped to android's amotion_event_action)
void kudroid_inject_touch_event(float x, float y, int action);
void kudroid_inject_touch_event_multi(float x, float y, int action, int pointerId, int pointerCount);

/// send java application lifecycle events (101=pause, 102=resume) to ui stream
void kudroid_send_lifecycle_event(int eventType);

///load DEX with KuART and list the found classes.
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_translate_dex(const char* dexPath);

///run vfs and i/o redirection autotest; returns a malloc log.
const char* kudroid_vfs_self_test_log(void);
const char* kudroid_vfs_extended_test_log(void);

///check KuART integration. Deprecated parameter (kept for ABI compatibility).
///returns a diagnostic log malloc; the caller must release it with free().
char* kudroid_test_jvm(const char* unused_path);
char* kudroid_test_gpu(void);
char* kudroid_test_audio(void);
const char* kudroid_run_so_test(const char* soPath, const char* entrypoint);

///load the arm64 gpu test .so file and execute its vulkan test via bionicshim blocking.
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_gpu_vulkan_so_test(const char* path);

///load the arm64 gpu test .so file and execute its opengl+egl test via bionicshim blocking.
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_gpu_opengl_so_test(const char* path);

///extract and install the apk's arm64-v8a root libraries into android_root.
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_install_apk(const char* apkPath);

///scans the installed apk's library folder and loads all its native (.so) libraries.
///returns a diagnostic log malloc; the caller must release it with free().
const char* kudroid_run_apk(const char* appName);

/// clears an application's internal cache folders.
/// returns 1 on success, 0 on failure.
int kudroid_clear_app_cache(const char* package_name);

/// completely delete an installed app and its data.
/// returns 1 on success, 0 on failure.
int kudroid_delete_app(const char* package_name);

/// delete app with progress report via callback (phase UTF-8, percent 0-100).
/// callback runs on the thread calling the function — Swift dispatches itself to the main thread.
typedef void (*kudroid_delete_progress_cb)(const char* phase, int percent, void* userdata);
int kudroid_delete_app_progress(const char* package_name,
                                kudroid_delete_progress_cb cb,
                                void* userdata);

/// detailed debug log of the last app deletion (path, write permission,
/// number of deleted files/failure, filesystem error...). Also written to file
///kudroid_uninstall_debug.txt in Documents. Returns the malloc string;
/// the caller must free with free().
const char* kudroid_uninstall_debug_log(void);

///gets basic information about an installed application.
/// returns a malloc string (e.g. json or formatted text); the caller must release it with free().
const char* kudroid_get_app_info(const char* package_name);

///check if the client application did not crash (gentle crash) during the last session.
/// returns 1 if there is a crash, 0 if normal.
int kudroid_has_crashed(void);

/// clear the crash state after the interface has displayed a notification.
void kudroid_clear_crash_state(void);

/// extract up to 30 last log lines before crash.
///return string malloc; the caller must free it with free().
const char* kudroid_get_last_crash_tail(void);

/// returns jit status: "JIT: Enabled" or "JIT: Disabled" (malloced).
const char* kudroid_jit_status(void);

///test jit with both CS_DEBUGGED and W^X (TrollStore) execution. Returns 1 if JIT exists, 0 otherwise.
int kudroid_is_jit_enabled(void);

// / ghi l i error kh i  ng v   t tr ng th i gentle crash
void kudroid_report_startup_error(const char* title, const char* message);

// Android Runtime Permission Manager APIs
int kudroid_check_permission(const char* packageName, const char* permissionName);
void kudroid_set_group_permission(const char* packageName, const char* groupKey, int granted);
int kudroid_is_group_granted(const char* packageName, const char* groupKey);
void kudroid_grant_all_permissions(const char* packageName);
const char* kudroid_get_app_permissions_json(const char* packageName);
void kudroid_set_app_permissions_json(const char* packageName, const char* jsonStr);

// Permission Dialog Prompt Callbacks
typedef void (*kudroid_permission_prompt_cb)(const char* packageName, const char* permissionsCsv, int requestCode, void* activityHandle);
void kudroid_set_permission_prompt_callback(kudroid_permission_prompt_cb cb);
void kudroid_prompt_permission_request(const char* packageName, const char* permissionsCsv, int requestCode, void* activityHandle);
void kudroid_submit_permission_response(void* activityHandle, int requestCode, const char* permissionsCsv, int granted);

// ── soft keyboard ────────────────────────────────────────────────────────────
//
// KuDroid ships no on-screen keyboard, so the guest's InputMethodManager forwards to
// the host and iOS shows the system keyboard for whichever view is first responder.
// The callbacks exist because kudroid_core is a static library and cannot reach UIKit:
// the view that must become first responder lives on the Swift side.

/// Registered by the host. `flags` mirrors InputMethodManager.showSoftInput.
typedef void (*kudroid_soft_input_show_cb)(int flags);
typedef void (*kudroid_soft_input_hide_cb)(void);

void kudroid_set_soft_input_callbacks(kudroid_soft_input_show_cb show,
                                      kudroid_soft_input_hide_cb hide);

/// Called from the guest to raise or dismiss the keyboard. Returns 1 when a host
/// callback was registered and invoked.
int kudroid_show_soft_input(int flags);
int kudroid_hide_soft_input(void);

/// Real keyboard visibility, published by the host. iOS can dismiss the keyboard on
/// its own, so the guest must read the state rather than assume its request stuck.
int kudroid_is_soft_input_visible(void);
void kudroid_set_soft_input_visible(int visible);

/// Text the user typed, delivered to the guest's focused InputConnection. UTF-8, and
/// may be more than one character: autocorrect, paste and astral-plane emoji all
/// arrive as a single insertion.
void kudroid_dispatch_text_input(const char* utf8);

/// Backspace from the host keyboard. Separate from text input because it deletes
/// relative to the cursor rather than inserting.
void kudroid_dispatch_delete_backward(void);

#ifdef __cplusplus
}
#endif
