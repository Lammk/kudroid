#include "dex_builder.h"
#include "kudroid/BionicShim.h"
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexJniEnv.h"

#include <cstdio>
#include <cstring>

using namespace kudroid::kuart;

namespace {
int failures = 0;
int checks = 0;
int load_calls = 0;
int render_calls = 0;
int initialize_calls = 0;
int unload_calls = 0;
int lookup_calls = 0;
bool long_symbol_available = false;
void* dummy = nullptr;

void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}

jboolean JNICALL Render(JNIEnv*, jclass) {
    ++render_calls;
    return JNI_TRUE;
}
jboolean JNICALL Load(JNIEnv* env, jclass, jstring path) {
    ++load_calls;
    const char* text = env->GetStringUTFChars(path, nullptr);
    bool correct_path = text && std::strcmp(text, "/native/libs") == 0;
    env->ReleaseStringUTFChars(path, text);
    jclass bridge = env->FindClass("DependentBridge");
    char name[] = "render", signature[] = "()Z";
    JNINativeMethod method{name, signature, reinterpret_cast<void*>(&Render)};
    return correct_path && env->RegisterNatives(bridge, &method, 1) == JNI_OK;
}
jboolean JNICALL Initialize(JNIEnv*, jclass) { ++initialize_calls; return JNI_FALSE; }
jboolean JNICALL Unload(JNIEnv*, jclass) { ++unload_calls; return JNI_FALSE; }
jboolean JNICALL Symbolic(JNIEnv*, jclass, jint value) { return value == 17 ? JNI_TRUE : JNI_FALSE; }

void* Lookup(const char* name) {
    ++lookup_calls;
    if (long_symbol_available && std::strcmp(name, "Java_BindingFixture_symbolic__I") == 0) {
        return reinterpret_cast<void*>(&Symbolic);
    }
    return dummy;
}

dexbuild::MethodSpec Native(const char* name) {
    dexbuild::MethodSpec method;
    method.name = name;
    method.return_type = "Z";
    method.access_flags = 0x109;
    return method;
}
}  // namespace

int main() {
    dexbuild::ClassSpec loader;
    loader.descriptor = "LNativeLoaderFixture;";
    auto load = Native("load");
    load.params = {"Ljava/lang/String;"};
    loader.direct_methods = {load, Native("initialize"), Native("unload")};
    dexbuild::ClassSpec bridge;
    bridge.descriptor = "LDependentBridge;";
    bridge.direct_methods = {Native("render")};
    dexbuild::ClassSpec binding;
    binding.descriptor = "LBindingFixture;";
    auto symbolic = Native("symbolic");
    symbolic.params = {"I"};
    binding.direct_methods = {symbolic};
    dexbuild::DexBuilder builder;
    auto dex = builder.Build({loader, bridge, binding});
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error) ||
        !linker.AddDexFile(dex.data(), dex.size(), "loader.dex", &error)) {
        std::printf("FAIL DEX: %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    JNIEnv* env = jni.env();
    jclass loaderClass = env->FindClass("NativeLoaderFixture");
    char loadName[] = "load", loadSig[] = "(Ljava/lang/String;)Z";
    char initName[] = "initialize", unloadName[] = "unload", boolSig[] = "()Z";
    JNINativeMethod methods[] = {
        {loadName, loadSig, reinterpret_cast<void*>(&Load)},
        {initName, boolSig, reinterpret_cast<void*>(&Initialize)},
        {unloadName, boolSig, reinterpret_cast<void*>(&Unload)},
    };
    Check(env->RegisterNatives(loaderClass, methods, 3) == JNI_OK, "Loader entry points register");
    jmethodID loadId = env->GetStaticMethodID(loaderClass, loadName, loadSig);
    jvalue path{};
    path.l = env->NewStringUTF("/native/libs");
    Check(env->CallStaticBooleanMethodA(loaderClass, loadId, &path) == JNI_TRUE && !env->ExceptionCheck(),
          "Native loader returns its actual result");
    Check(load_calls == 1, "Registered native loader executes instead of a success shortcut");
    jclass bridgeClass = env->FindClass("DependentBridge");
    auto* render = linker.FindClass(bridge.descriptor.c_str())->FindDirectMethod("render", "()Z");
    Check(render->native_fn == reinterpret_cast<void*>(&Render), "Loader registers dependent callbacks");
    if (render->native_fn) {
        jmethodID renderId = env->GetStaticMethodID(bridgeClass, "render", "()Z");
        Check(env->CallStaticBooleanMethodA(bridgeClass, renderId, nullptr) == JNI_TRUE && render_calls == 1,
              "Dependent callback executes after native loading");
    }
    jmethodID initId = env->GetStaticMethodID(loaderClass, initName, boolSig);
    Check(env->CallStaticBooleanMethodA(loaderClass, initId, nullptr) == JNI_FALSE && initialize_calls == 1,
          "Initializer failure is preserved");
    jmethodID unloadId = env->GetStaticMethodID(loaderClass, unloadName, boolSig);
    Check(env->CallStaticBooleanMethodA(loaderClass, unloadId, nullptr) == JNI_FALSE && unload_calls == 1,
          "Native unload executes and preserves its result");

    dummy = kudroid::resolve_bionic_symbol("absent_native_fixture_symbol");
    Check(dummy && kudroid::is_universal_dummy(dummy), "Missing-symbol fixture returns the universal dummy");
    jni.set_symbol_lookup(&Lookup);
    auto* native = linker.FindClass(binding.descriptor.c_str())->FindDirectMethod("symbolic", "(I)Z");
    Check(!jni.LinkNativeMethod(native) && native->native_fn == nullptr && lookup_calls == 2,
          "JNI rejects dummy results from both short and long symbol lookups");
    jclass bindingClass = env->FindClass("BindingFixture");
    jmethodID symbolicId = env->GetStaticMethodID(bindingClass, "symbolic", "(I)Z");
    jvalue value{};
    value.i = 17;
    env->CallStaticBooleanMethodA(bindingClass, symbolicId, &value);
    DexObject* exception = jni.pending_exception();
    Check(exception && exception->clazz &&
          std::strcmp(exception->clazz->descriptor, "Ljava/lang/UnsatisfiedLinkError;") == 0,
          "An unresolved JNI method throws UnsatisfiedLinkError");
    env->ExceptionClear();
    long_symbol_available = true;
    lookup_calls = 0;
    Check(jni.LinkNativeMethod(native) && native->native_fn == reinterpret_cast<void*>(&Symbolic) && lookup_calls == 2,
          "A real long symbol resolves after an earlier miss and a dummy short symbol");
    Check(env->CallStaticBooleanMethodA(bindingClass, symbolicId, &value) == JNI_TRUE && !env->ExceptionCheck(),
          "Retried native binding executes with the correct argument");
    native->native_fn = dummy;
    long_symbol_available = false;
    Check(!jni.LinkNativeMethod(native) && native->native_fn == nullptr,
          "A cached dummy pointer is not accepted as a JNI binding");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
