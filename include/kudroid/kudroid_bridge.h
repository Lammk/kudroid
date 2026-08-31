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

/// completely clears all diagnostic log files (android, stderr, crash, breadcrumbs, etc.)
void kudroid_clear_all_logs(void);

/// sets the documents directory used by vfspathremapper.
void kudroid_set_documents_dir(const char* dir);

int kudroid_is_jit_enabled(void);

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

///set the cametallayer or uiview cursor used for anativewindow surface constraints.
void kudroid_set_metal_layer(void* layer, int width, int height, float density);
void kudroid_unbind_metal_layer(void);

///run vfs and i/o redirection autotest; returns a malloc log.
const char* kudroid_vfs_self_test_log(void);
const char* kudroid_vfs_extended_test_log(void);

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
// The guest asks for a keyboard through InputMethodManager; the host owns the only
// thing that can produce one. KuDroid has no on-screen keyboard of its own, so the
// request is forwarded to iOS, which shows the system keyboard for whichever view
// becomes first responder.
//
// Two directions, deliberately separate:
//   - show/hide travel guest -> host, through the callbacks the host registers.
//   - typed text travels host -> guest, through kudroid_dispatch_text_input.
//
// The callbacks exist because kudroid_core cannot reach UIKit: it is a static library
// linked into a Swift app, and the view that must become first responder lives there.

/// Registered by the host. `flags` mirrors InputMethodManager.showSoftInput.
typedef void (*kudroid_soft_input_show_cb)(int flags);
typedef void (*kudroid_soft_input_hide_cb)(void);

void kudroid_set_soft_input_callbacks(kudroid_soft_input_show_cb show,
                                      kudroid_soft_input_hide_cb hide);

/// Called from the guest (InputMethodManager) to raise or dismiss the keyboard.
/// Returns 1 when a host callback was registered and invoked, 0 otherwise — the
/// guest reports success either way, because an app told the keyboard cannot be
/// shown disables its own text entry.
int kudroid_show_soft_input(int flags);
int kudroid_hide_soft_input(void);

/// True while the host reports the keyboard as visible. Lets the guest answer
/// isActive()/isAcceptingText() with the real state instead of a constant.
int kudroid_is_soft_input_visible(void);
void kudroid_set_soft_input_visible(int visible);

/// Text the user typed on the host keyboard, delivered to the guest's focused
/// InputConnection. UTF-8; may be more than one character (autocorrect, paste, an
/// emoji outside the BMP).
void kudroid_dispatch_text_input(const char* utf8);

/// Backspace from the host keyboard. Separate from text input because it deletes
/// relative to the cursor rather than inserting, and IME semantics differ.
void kudroid_dispatch_delete_backward(void);

/// Records the directory the guest's .so files were scanned from, so a later query
/// can name a library that exists on disk but was never loaded.
void kudroid_set_native_lib_dir(const char* dir);

/// Absolute host path of the guest native library called `name`, for
/// BaseDexClassLoader.findLibrary().
///
/// `name` may be the bare library name from android.app.lib_name ("minecraftpe"),
/// a file name ("libminecraftpe.so"), or a path ending in one. The answer is the
/// real on-device path — under the iOS app container, not the Android
/// /data/app/... path the guest otherwise sees — because the caller passes it
/// straight to dlopen.
///
/// Writes into `out` (NUL-terminated) and returns 1 on success. Returns 0 and
/// leaves `out` untouched when there is no such library, when `out_size` is too
/// small, or on a null argument. Caller-supplied buffer rather than a returned
/// pointer: this is reachable from any guest thread, and a shared static would
/// hand out a string another thread is overwriting.
int kudroid_find_native_library(const char* name, char* out, unsigned long out_size);

#ifdef __cplusplus
}
#endif
