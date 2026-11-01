#include "dex_builder.h"
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/JniLibraryRegistry.h"
#include "kudroid/kuart/LibCore.h"

#include <atomic>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>

using namespace kudroid::kuart;

namespace {
int failures = 0;
int checks = 0;
JNIEnv* guest_env = nullptr;
bool application_available = false;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}

int LoadLibrary(const char*) {
    JNIEnv* env = guest_env;
    jclass threadClass = env->FindClass("android/app/ActivityThread");
    jmethodID current = env->GetStaticMethodID(threadClass, "currentActivityThread",
                                              "()Landroid/app/ActivityThread;");
    jobject thread = env->CallStaticObjectMethodA(threadClass, current, nullptr);
    jmethodID getApp = env->GetMethodID(threadClass, "getApplication", "()Landroid/app/Application;");
    jobject app = thread ? env->CallObjectMethodA(thread, getApp, nullptr) : nullptr;
    application_available = thread && app && !env->ExceptionCheck();
    return application_available;
}

void JNICALL OnCreate(JNIEnv* env, jobject) {
    jclass runtimeClass = env->FindClass("java/lang/Runtime");
    jmethodID getRuntime = env->GetStaticMethodID(runtimeClass, "getRuntime", "()Ljava/lang/Runtime;");
    jobject runtime = env->CallStaticObjectMethodA(runtimeClass, getRuntime, nullptr);
    jmethodID load = env->GetMethodID(runtimeClass, "loadLibrary", "(Ljava/lang/String;)V");
    jvalue library{};
    library.l = env->NewStringUTF("startup");
    env->CallVoidMethodA(runtime, load, &library);
    Check(!env->ExceptionCheck(), "Native library initialization during Application.onCreate succeeds");
    jclass looperClass = env->FindClass("android/os/Looper");
    jmethodID getMain = env->GetStaticMethodID(looperClass, "getMainLooper", "()Landroid/os/Looper;");
    jobject main = env->CallStaticObjectMethodA(looperClass, getMain, nullptr);
    jmethodID getQueue = env->GetMethodID(looperClass, "getQueue", "()Landroid/os/MessageQueue;");
    jobject queue = env->CallObjectMethodA(main, getQueue, nullptr);
    jclass queueClass = env->FindClass("android/os/MessageQueue");
    jmethodID quit = env->GetMethodID(queueClass, "quitInternal", "()V");
    env->CallVoidMethodA(queue, quit, nullptr);
}
}  // namespace

int main() {
    JniLibraryRegistry registry;
    int calls = 0;
    Check(registry.Initialize("one", [&] {
        ++calls;
        return registry.Initialize("one", [&] { ++calls; return false; });
    }), "Recursive load on the initializer thread succeeds");
    Check(registry.Initialize("one", [&] { ++calls; return false; }) && calls == 1,
          "Repeated load preserves the successful result");
    Check(!registry.Initialize("bad", [&] { ++calls; return false; }), "Initializer failure is returned");
    int before = calls;
    Check(!registry.Initialize("bad", [&] { ++calls; return true; }) && calls == before,
          "Failed initialization is not rerun");
    try {
        registry.Initialize("throw", []() -> bool { throw std::runtime_error("failed"); });
    } catch (const std::runtime_error&) {}
    Check(!registry.Initialize("throw", [] { return true; }), "Thrown initializer releases waiting callers");

    std::promise<void> entered;
    auto started = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future().share();
    std::atomic<int> concurrent_calls{0};
    bool first = false, second = false;
    std::thread a([&] {
        first = registry.Initialize("concurrent", [&] {
            ++concurrent_calls;
            entered.set_value();
            released.wait();
            return true;
        });
    });
    started.wait();
    std::thread b([&] { second = registry.Initialize("concurrent", [&] { ++concurrent_calls; return false; }); });
    Check(registry.Initialize("independent", [] { return true; }), "Unrelated libraries can initialize concurrently");
    release.set_value();
    a.join(); b.join();
    Check(first && second && concurrent_calls == 1, "Concurrent loads share one initializer result");
    Check(JniLibraryRegistry::IsSupportedVersion(0x10002) &&
          JniLibraryRegistry::IsSupportedVersion(0x10004) &&
          JniLibraryRegistry::IsSupportedVersion(0x10006), "Android JNI versions are accepted");
    Check(!JniLibraryRegistry::IsSupportedVersion(0) &&
          !JniLibraryRegistry::IsSupportedVersion(-1) &&
          !JniLibraryRegistry::IsSupportedVersion(0x60006), "Invalid JNI versions are rejected");

    dexbuild::ClassSpec app;
    app.descriptor = "LStartupApplication;";
    app.superclass = "Landroid/app/Application;";
    dexbuild::MethodSpec ctor;
    ctor.name = "<init>";
    ctor.access_flags = 0x10001;
    ctor.registers_size = ctor.ins_size = 1;
    ctor.code = {0x0e};
    app.direct_methods.push_back(ctor);
    dexbuild::MethodSpec onCreate;
    onCreate.name = "onCreate";
    onCreate.access_flags = 0x101;
    app.virtual_methods.push_back(onCreate);
    dexbuild::DexBuilder builder;
    auto dex = builder.Build({app});
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error) ||
        !linker.AddDexFile(dex.data(), dex.size(), "startup.dex", &error)) {
        std::printf("FAIL DEX: %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    guest_env = jni.env();
    char methodName[] = "onCreate", signature[] = "()V";
    JNINativeMethod native{methodName, signature, reinterpret_cast<void*>(&OnCreate)};
    Check(jni.RegisterNatives(linker.FindClass(app.descriptor.c_str()), &native, 1) == JNI_OK,
          "Startup native hook registers without initializing the Application");
    LibCoreSetLoadLibraryCallback(&LoadLibrary);
    jclass at = guest_env->FindClass("android/app/ActivityThread");
    jmethodID main = guest_env->GetStaticMethodID(at, "main", "([Ljava/lang/String;)V");
    jclass str = guest_env->FindClass("java/lang/String");
    jobjectArray args = guest_env->NewObjectArray(3, str, nullptr);
    guest_env->SetObjectArrayElement(args, 0, guest_env->NewStringUTF("test.startup"));
    guest_env->SetObjectArrayElement(args, 1, guest_env->NewStringUTF(""));
    guest_env->SetObjectArrayElement(args, 2, guest_env->NewStringUTF("StartupApplication"));
    jvalue launch{};
    launch.l = args;
    guest_env->CallStaticVoidMethodA(at, main, &launch);
    Check(application_available, "ActivityThread and Application exist when Java requests native initialization");
    Check(!guest_env->ExceptionCheck(), "ActivityThread exits without a pending exception");
    LibCoreSetLoadLibraryCallback(nullptr);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
