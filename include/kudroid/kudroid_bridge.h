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
void kudroid_set_metal_layer(void* layer, int width, int height);
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

#ifdef __cplusplus
}
#endif