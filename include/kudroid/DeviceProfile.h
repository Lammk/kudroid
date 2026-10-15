// The device identity KuDroid reports to guest code.
//
// Every surface an app can read its platform version from has to give the same
// answer, and they used to disagree: Build.VERSION.SDK_INT said 29,
// /system/build.prop said 34, AConfiguration_getSdkVersion() said 30, and
// ANativeActivity::sdkVersion said 29. Apps branch on API level constantly —
// scoped storage, runtime permissions, MediaStore, notification channels — so a
// single run could take the pre-Q path in Java and the post-T path in native code,
// in the same process, over the same files.
//
// 29 (Android 10) is the level KuART's own behaviour was ported from, so it is the
// only value the rest of the runtime is consistent with. Raising it means auditing
// the framework for the APIs each level requires, not editing one number.
//
// Not app-specific and not derived from the APK: an app's targetSdkVersion says what
// it was BUILT against, never what the device runs, so it must not be echoed back
// here. Android reports the platform's own level to every app alike.
#ifndef KUDROID_DEVICE_PROFILE_H
#define KUDROID_DEVICE_PROFILE_H

// Keep in step with framework/android/os/Build.java, which cannot include this
// header. Build.VERSION.SDK_INT and Build.VERSION.RELEASE are the Java-visible
// copies of these two.
#define KUDROID_SDK_INT 29
#define KUDROID_SDK_INT_STR "29"
#define KUDROID_ANDROID_RELEASE "10"

// One device identity for build.prop, the property service and Build.java alike.
// The ABI list is what an app reads to decide which native library set to load, so
// it has to match what the ELF loader can actually map — arm64 only.
#define KUDROID_DEVICE_MODEL "KuDroid"
#define KUDROID_DEVICE_MANUFACTURER "KuDroid"
#define KUDROID_DEVICE_BRAND "kudroid"
#define KUDROID_DEVICE_NAME "kudroid"
#define KUDROID_DEVICE_BOARD "kudroid_arm64"
#define KUDROID_DEVICE_ABI "arm64-v8a"

#endif  // KUDROID_DEVICE_PROFILE_H
