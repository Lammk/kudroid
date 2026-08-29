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

// Register what AndroidManifest.xml declared, before launching anything.
//
// Manifest data is loaded separately from starting the app because they are separate
// concerns and because a component reads its own manifest entry while constructing
// itself — by the time kuart_launch_app runs it is already too late. AGDK's
// GameActivity is the case that proves it: its onCreate does
//
//   getPackageManager().getActivityInfo(getIntent().getComponent(), GET_META_DATA)
//       .metaData.getString("android.app.lib_name")
//
// to find the .so holding its renderer, so an activity launched before its meta-data
// exists gets no native library and draws nothing.
//
// Passing this through kuart_launch_app's String[] was the alternative and is worse:
// the data is a map per component, the argument is a flat array, and the reader is
// PackageManager rather than ActivityThread — so it would mean a hand-written
// serialiser on each side plus a detour through a class that has no use for it.
//
// Call once per component before kuart_launch_app. `component_name` NULL or empty
// registers the <application> element. `keys`/`values` are parallel arrays of
// `count` entries; values are strings because that is what AXML stores.
void kuart_register_component_meta_data(const char* component_name,
                                       const char* const* keys,
                                       const char* const* values,
                                       int count);

// Register the package name and the activity list, for PackageManager queries that
// name the running package.
void kuart_register_package(const char* package_name,
                            const char* const* activities,
                            int activity_count);

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

// Save/restore the interpreter's per-thread bookkeeping around a call that may
// leave by a route which does not unwind the C++ stack.
//
// The JNI_OnLoad shield in kudroid_bridge.cpp siglongjmps out of a library that
// faults mid-callback. If that library had called back into Java, the interpreter
// frames it entered are skipped over without running their destructors, so the
// interpreter keeps believing they are live: its call-stack vector retains
// pointers into reclaimed C++ stack, and its depth counter never comes back down.
//
// The stale entries are not merely untidy. The next exception on that thread
// renders a stack trace from them and dereferences freed memory — a SIGSEGV inside
// the diagnostic path, which is how Minecraft's MainActivity.onCreate failure
// stayed invisible. Bracket the guarded call with these and the interpreter is
// consistent again whichever way the call came back.
//
// `out_state` is an opaque scratch area owned by the caller; KUART_THREAD_STATE_WORDS
// is sized with headroom so adding another counter does not change the ABI.
#define KUART_THREAD_STATE_WORDS 4
void kuart_save_thread_state(size_t out_state[KUART_THREAD_STATE_WORDS]);
void kuart_restore_thread_state(const size_t state[KUART_THREAD_STATE_WORDS]);

// Dispatch text typed on the host keyboard into the guest's focused
// InputConnection.
//
// Goes through ActivityThread rather than straight into the connection so it lands
// on the Looper thread: an InputConnection edits the same buffer the app's UI reads,
// and iOS delivers key input on its own main thread. `utf8` may hold more than one
// character — autocorrect replacements, paste, and astral-plane emoji all arrive as
// a single insertion.
void kuart_dispatch_text_input(const char* utf8);

// Backspace from the host keyboard. Separate from text input because it deletes
// relative to the cursor rather than inserting.
void kuart_dispatch_delete_backward(void);

const char* kuart_last_error(void);

#ifdef __cplusplus
}
#endif

#endif  // KUDROID_KUART_RUNTIME_H
