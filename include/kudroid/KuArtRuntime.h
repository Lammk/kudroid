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

// Set the path to write missing framework classes log (classes.log).
void kuart_set_missing_class_log_path(const char* path);

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

// Launch the app: bootstrap AppComponentFactory + Application, then walk the
// activity candidates.
//
// Everything here comes from AndroidManifest.xml; pass NULL or "" for what the
// manifest does not declare. Android runs the factory and the Application before
// any component, and apps depend on that ordering — their static initialisers are
// where build tooling and DI frameworks put setup that later code assumes is done.
//
//   package_name        <manifest package>
//   component_factory   <application android:appComponentFactory>
//   app_class           <application android:name>
//   activity_name       the launcher activity
//   extra_candidates    remaining manifest activities, in declaration order
int kuart_launch_app(const char* package_name, const char* component_factory,
                     const char* app_class, const char* activity_name,
                     const char* const* extra_candidates, int extra_count);

// Launch with no manifest-declared Application or factory.
int kuart_launch_activity(const char* activity_name, const char* const* extra_candidates,
                          int extra_count);

// Dispatch lifecycle events.
void kuart_send_lifecycle_event(int event_type);

// Dispatch touch events.
void kuart_post_touch_event(int action, float x, float y);

// Take the in-flight Java exception of the CALLING thread, if any, and clear it.
//
// A native library called from Java (System.loadLibrary -> JNI_OnLoad) can call
// back into Java and leave an exception pending. JNI requires the native side to
// check and clear it; libraries that ignore the rule leave the exception in place,
// and the interpreter then reports it as if the Java call that triggered the
// library had thrown. That is what stopped Minecraft: an exception raised inside a
// JNI_OnLoad callback surfaced from System.loadLibrary, which the app only guards
// with catch(UnsatisfiedLinkError), so MainActivity.<clinit> died.
//
// Returns 1 when an exception was taken; `out` (may be null) receives a
// human-readable description with the Java stack trace, valid until the next call
// on the same thread.
int kuart_take_pending_exception(const char** out);

const char* kuart_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // KUDROID_KUART_RUNTIME_H
