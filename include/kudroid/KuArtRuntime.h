// KuART Runtime: the single entry point that KuDroid uses to run Java bytecode.
// Replaces dex2jar -> classes.jar -> Avian JVM pipeline. KuART interprets DEX directly.
#ifndef KUDROID_KUART_RUNTIME_H
#define KUDROID_KUART_RUNTIME_H

#include <jni.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Runtime logging callback.
void kuart_set_log_callback(void (*cb)(const char* message));

// Hook to look up guest native symbols for linking native methods.
void kuart_set_symbol_lookup(void* (*fn)(const char* symbol));

// Hook to load guest native libraries when Java calls System.loadLibrary/Runtime.loadLibrary.
void kuart_set_load_library_callback(int (*cb)(const char* libname));

// Load embedded framework.dex and all classes*.dex in `app_dir`.
int kuart_init(const char* app_dir);

void kuart_shutdown(void);

// Returns 1 if kuart_init succeeded.
int kuart_is_ready(void);

// JavaVM/JNIEnv for native code to call into (JNI_OnLoad, RegisterNatives, etc.).
JavaVM* kuart_get_javavm(void);
jint kuart_get_env(JavaVM* vm, void** env, jint version);

// Number of loaded DEX files and resolved classes.
size_t kuart_num_dex_files(void);
size_t kuart_num_loaded_classes(void);

// Check if class extends android.app.Activity.
int kuart_class_extends_activity(const char* class_name);

// List app classes (excluding framework/SDK).
size_t kuart_list_app_classes(char** out, size_t max_out);
void kuart_free_class_list(char** list, size_t count);

// Launch ActivityThread.main.
int kuart_launch_activity(const char* activity_name, const char* const* extra_candidates,
                          int extra_count);

// Dispatch lifecycle events.
void kuart_send_lifecycle_event(int event_type);

// Dispatch touch events.
void kuart_post_touch_event(int action, float x, float y);

const char* kuart_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // KUDROID_KUART_RUNTIME_H
