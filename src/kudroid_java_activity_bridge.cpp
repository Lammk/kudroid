#include "kudroid/kudroid_jni.h"
#include <cstdio>
#include <cstdarg>

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

static void app_log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kudroid_android_log_message(4 /* INFO */, "KuDroidApp", buf);
}

// khởi chạy activity thông qua activitythread để có looper xử lý vòng đời.
// extraCandidates: danh sách tên class dự phòng (đã verify ở tầng C++),
// ActivityThread sẽ thử lần lượt nếu candidate chính không load được —
// tổng quát cho mọi app, không hardcode tên riêng.
extern "C" void kudroid_launch_java_activity(JavaVM* vm, const char* activityName,
                                             const char* const* extraCandidates,
                                             int extraCandidateCount) {
    app_log("kudroid_launch_java_activity: requesting launch for %s", activityName ? activityName : "NULL");
    if (!vm || !activityName) return;

    JNIEnv* env = nullptr;
    if (kudroid_jni_get_env(vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
        app_log("ERROR: failed to get JNIEnv!");
        return;
    }

    // Ladder Probes: Kiểm tra từng bậc thang từ class đơn giản đến phức tạp
    const char* probeClasses[] = {
        "java/lang/String",
        "android/os/Looper",
        "android/os/Message",
        "android/os/Handler",
        "android/app/Activity",
        "android/app/ActivityThread"
    };

    for (const char* clsName : probeClasses) {
        app_log("[LadderProbe] FindClass('%s')...", clsName);
        jclass c = env->FindClass(clsName);
        if (c) {
            app_log("[LadderProbe] -> SUCCESS: Found '%s' (jclass=%p)", clsName, (void*)c);
            env->DeleteLocalRef(c);
        } else {
            app_log("[LadderProbe] -> FAILED: '%s'", clsName);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
    }
    
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (!atClass) {
        app_log("ERROR: could not find android/app/ActivityThread in classpath!");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    
    jmethodID mainMethod = env->GetStaticMethodID(atClass, "main", "([Ljava/lang/String;)V");
    if (!mainMethod) {
        app_log("ERROR: could not find main([Ljava/lang/String;)V in ActivityThread!");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return;
    }
    
    // Truyền args cho main(String[]): [0] = candidate chính,
    // [1..] = fallback candidates (đã verify). Java side tự sinh thêm
    // biến thể đoán từ package prefix nếu cần.
    jclass stringClass = env->FindClass("java/lang/String");
    const int totalArgs = 1 + (extraCandidates ? extraCandidateCount : 0);
    jobjectArray args = env->NewObjectArray(totalArgs, stringClass, nullptr);
    jstring arg0 = env->NewStringUTF(activityName);
    env->SetObjectArrayElement(args, 0, arg0);
    env->DeleteLocalRef(arg0);
    for (int i = 0; i < extraCandidateCount && extraCandidates && extraCandidates[i]; ++i) {
        if (!extraCandidates[i][0]) continue;
        jstring s = env->NewStringUTF(extraCandidates[i]);
        if (s) {
            env->SetObjectArrayElement(args, 1 + i, s);
            env->DeleteLocalRef(s);
        }
    }
    
    app_log("Starting ActivityThread.main (entering UI event loop)...");
    env->CallStaticVoidMethod(atClass, mainMethod, args);
    
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        jthrowable exc = env->ExceptionOccurred();
        env->ExceptionClear();
        if (exc) {
            jclass excCls = env->GetObjectClass(exc);
            jmethodID toStringMid = env->GetMethodID(excCls, "toString", "()Ljava/lang/String;");
            if (toStringMid) {
                jstring str = (jstring)env->CallObjectMethod(exc, toStringMid);
                if (str) {
                    const char* cstr = env->GetStringUTFChars(str, nullptr);
                    app_log("UNCAUGHT JAVA EXCEPTION: %s", cstr ? cstr : "unknown");
                    env->ReleaseStringUTFChars(str, cstr);
                }
            }
        }
    }
}

// gửi sự kiện vòng đời (pause, resume) vào luồng ui
extern "C" void kudroid_send_lifecycle_event(int eventType) {
    fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: preparing to send event=%d\n", eventType);
    JavaVM* vm = kudroid_jni_get_javavm();
    if (!vm) {
        fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: ERROR no JVM found!\n");
        return;
    }
    
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm->AttachCurrentThread((void**)&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    
    jclass atClass = env->FindClass("android/app/ActivityThread");
    if (atClass) {
        jmethodID postMethod = env->GetStaticMethodID(atClass, "postLifecycleEvent", "(ILjava/lang/String;)V");
        if (postMethod) {
            fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: successfully calling postLifecycleEvent(%d)\n", eventType);
            env->CallStaticVoidMethod(atClass, postMethod, eventType, nullptr);
        } else {
            fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: ERROR could not find postLifecycleEvent method\n");
        }
    } else {
        fprintf(stdout, "[KuDroidApp] kudroid_send_lifecycle_event: ERROR could not find ActivityThread class\n");
    }
    
    if (attached) {
        vm->DetachCurrentThread();
    }
}
