#ifndef KUDROID_JNI_H
#define KUDROID_JNI_H

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initializes the JVM mapping and returns the global JavaVM pointer.
// If the JVM is not initialized, it will be initialized here.
jint kudroid_jni_get_env(JavaVM* vm, void** env, jint version);

// Creates and initializes the Avian JVM instance if it hasn't been already.
void kudroid_jni_init_jvm(const char* bootclasspath, const char* classpath);

// Destroys the Avian JVM instance and cleans up.
void kudroid_jni_destroy_jvm(void);

// Retrieves the global JavaVM instance used by kudroid.
JavaVM* kudroid_jni_get_javavm(void);

#ifdef __cplusplus
}
#endif

#endif // KUDROID_JNI_H
