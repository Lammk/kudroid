#include "dex_builder.h"
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexJniEnv.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace dexbuild;
using namespace kudroid::kuart;

namespace {
int failures = 0;
int checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}

std::vector<ClassSpec> Classes() {
    std::vector<ClassSpec> classes;
    for (const char* name : {"Method", "StaticMethod", "Field", "StaticField", "Reflect", "Bad"}) {
        ClassSpec c;
        c.descriptor = std::string("L") + name + ";";
        c.static_fields = {{"count", "I", 0x9}};
        c.instance_fields = {{"value", "I", 0x1}};
        MethodSpec clinit;
        clinit.name = "<clinit>";
        clinit.access_flags = 0x10008;
        clinit.registers_size = 1;
        clinit.code = {0x0e};
        MethodSpec ctor;
        ctor.name = "<init>";
        ctor.access_flags = 0x10001;
        ctor.registers_size = ctor.ins_size = 1;
        ctor.code = {0x0e};
        MethodSpec stat;
        stat.name = "staticMethod";
        stat.access_flags = 0x9;
        stat.code = {0x0e};
        MethodSpec instance;
        instance.name = "instanceMethod";
        instance.registers_size = instance.ins_size = 1;
        instance.code = {0x0e};
        c.direct_methods = {clinit, ctor, stat};
        c.virtual_methods = {instance};
        classes.push_back(c);
    }
    return classes;
}

bool ExceptionIs(DexJniEnv& jni, const char* descriptor) {
    DexObject* ex = jni.pending_exception();
    return ex && ex->clazz && std::string(ex->clazz->descriptor) == descriptor;
}
}  // namespace

int main() {
    auto classes = Classes();
    DexBuilder indices;
    indices.Build(classes);
    for (auto& c : classes) {
        auto& init = c.direct_methods[0];
        if (c.descriptor == "LBad;") {
            init.code = {0x12, 0x27};  // Throw a null reference.
        } else {
            uint16_t count = indices.FieldIndexOf(c.descriptor, c.static_fields[0]);
            init.code = {0x60, count, 0x00d8, 0x0100, 0x67, count, 0x0e};
        }
    }
    DexBuilder builder;
    auto dex = builder.Build(classes);
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error) ||
        !linker.AddDexFile(dex.data(), dex.size(), "initialization.dex", &error)) {
        std::printf("FAIL loading DEX: %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    JNIEnv* env = jni.env();
    JavaVM* vm = nullptr;
    Check(env->GetJavaVM(&vm) == JNI_OK && vm, "JNI exposes its JavaVM");
    void* queried = env;
    Check(vm->GetEnv(&queried, 0x60006) == JNI_EVERSION && queried == nullptr,
          "JavaVM.GetEnv rejects an unsupported version and clears its output");
    Check(vm->GetEnv(&queried, JNI_VERSION_1_6) == JNI_OK && queried == env,
          "JavaVM.GetEnv accepts an Android JNI version");
    for (const char* name : {"Method", "StaticMethod", "Field", "StaticField"}) {
        jclass handle = env->FindClass(name);
        DexClass* klass = linker.FindClass((std::string("L") + name + ";").c_str());
        Check(handle && klass, "FindClass resolves a class");
        if (!handle || !klass) continue;
        Check(klass->status == DexClass::Status::kLinked, "FindClass links without initializing");
        auto* count = klass->FindStaticField("count", "I");
        Check(klass->static_values[count->offset_or_slot].i == 0, "FindClass has no initializer side effects");
        const std::string api(name);
        bool found = api == "Method" ? env->GetMethodID(handle, "instanceMethod", "()V") != nullptr :
                     api == "StaticMethod" ? env->GetStaticMethodID(handle, "staticMethod", "()V") != nullptr :
                     api == "Field" ? env->GetFieldID(handle, "value", "I") != nullptr :
                     env->GetStaticFieldID(handle, "count", "I") != nullptr;
        Check(found && !env->ExceptionCheck(), "Member lookup succeeds");
        Check(klass->status == DexClass::Status::kInitialized, "Member lookup initializes its class");
        env->GetStaticFieldID(handle, "count", "I");
        Check(klass->static_values[count->offset_or_slot].i == 1, "Initializer executes exactly once");
        Check(env->GetMethodID(handle, "staticMethod", "()V") == nullptr &&
              ExceptionIs(jni, "Ljava/lang/NoSuchMethodError;"), "Instance lookup rejects a static method");
        env->ExceptionClear();
        Check(env->GetStaticMethodID(handle, "instanceMethod", "()V") == nullptr &&
              ExceptionIs(jni, "Ljava/lang/NoSuchMethodError;"), "Static lookup rejects an instance method");
        env->ExceptionClear();
    }

    jclass classClass = env->FindClass("java/lang/Class");
    jmethodID forName = env->GetStaticMethodID(classClass, "forName",
        "(Ljava/lang/String;ZLjava/lang/ClassLoader;)Ljava/lang/Class;");
    jvalue args[3]{};
    args[0].l = env->NewStringUTF("Reflect");
    args[1].z = JNI_FALSE;
    jobject reflected = env->CallStaticObjectMethodA(classClass, forName, args);
    auto* reflectClass = linker.FindClass("LReflect;");
    Check(reflected && !env->ExceptionCheck(), "Class.forName(false) resolves the class");
    Check(reflectClass->status == DexClass::Status::kLinked, "Class.forName(false) does not initialize");
    args[1].z = JNI_TRUE;
    Check(env->CallStaticObjectMethodA(classClass, forName, args) != nullptr &&
          reflectClass->status == DexClass::Status::kInitialized, "Class.forName(true) initializes");

    jclass bad = env->FindClass("Bad");
    Check(bad && !env->ExceptionCheck(), "FindClass does not execute a failing initializer");
    Check(env->GetMethodID(bad, "instanceMethod", "()V") == nullptr &&
          env->ExceptionCheck(), "Failed initializer prevents member lookup");
    env->ExceptionClear();
    for (int api = 0; api < 4; ++api) {
        bool rejected = api == 0 ? env->GetMethodID(bad, "instanceMethod", "()V") == nullptr :
                        api == 1 ? env->GetStaticMethodID(bad, "staticMethod", "()V") == nullptr :
                        api == 2 ? env->GetFieldID(bad, "value", "I") == nullptr :
                                   env->GetStaticFieldID(bad, "count", "I") == nullptr;
        Check(rejected && ExceptionIs(jni, "Ljava/lang/NoClassDefFoundError;"),
              "Erroneous class lookup reports NoClassDefFoundError");
        env->ExceptionClear();
    }
    jmethodID newInstance = env->GetMethodID(classClass, "newInstance", "()Ljava/lang/Object;");
    Check(env->CallObjectMethod(bad, newInstance) == nullptr &&
          ExceptionIs(jni, "Ljava/lang/NoClassDefFoundError;"), "Class.newInstance preserves initialization failure instead of reporting OOM");
    env->ExceptionClear();
    Check(env->AllocObject(bad) == nullptr && ExceptionIs(jni, "Ljava/lang/NoClassDefFoundError;"),
          "JNI AllocObject preserves initialization failure");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
