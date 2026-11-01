#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace kudroid::kuart;

namespace {
int checks = 0;
int failures = 0;
void Check(bool ok, const char* message) {
    ++checks;
    std::printf("%s %s\n", ok ? "OK" : "FAIL", message);
    if (!ok) ++failures;
}

DexValue Call(Interpreter& interp, DexObject* map, const char* name, const char* sig,
              std::vector<DexValue> args = {}) {
    DexMethod* method = map->clazz->FindVirtualMethod(name, sig);
    Check(method && method->code_item, name);
    if (!method || !method->code_item) return {};
    args.insert(args.begin(), DexValue::Ref(map));
    DexValue result = interp.Execute(method, args.data(), args.size());
    Check(!interp.HasPendingException(), "map operation succeeds");
    interp.ClearPendingException();
    return result;
}

bool IsString(DexValue value, const char* expected) {
    auto* string = reinterpret_cast<DexString*>(value.l);
    return string && string->utf8 && std::strcmp(string->utf8, expected) == 0;
}
}  // namespace

int main() {
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("FAIL %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    DexClass* klass = linker.FindClass("Ljava/util/concurrent/ConcurrentHashMap;");
    Check(klass && !klass->is_stub && interp.EnsureInitialized(klass), "real concurrent-map class");
    if (!klass || interp.HasPendingException()) return 1;
    DexMethod* ctor = klass->FindDirectMethod("<init>", "(IFI)V");
    Check(ctor && ctor->code_item, "ConcurrentHashMap(int,float,int) has a body");
    if (!ctor || !ctor->code_item) return 1;

    auto str = [&](const char* text) {
        return DexValue::Ref(reinterpret_cast<DexObject*>(linker.NewString(text)));
    };
    struct Sizing { int capacity; float factor; int concurrency; };
    const Sizing valid[] = {{0, 0.75f, 1}, {16, 0.5f, 4},
        {1, std::numeric_limits<float>::infinity(), std::numeric_limits<int>::max()}};
    for (const auto& sizing : valid) {
        DexObject* map = linker.AllocObject(klass);
        DexValue args[] = {DexValue::Ref(map), DexValue::Int(sizing.capacity),
                          DexValue::Float(sizing.factor), DexValue::Int(sizing.concurrency)};
        interp.Execute(ctor, args, 4);
        Check(!interp.HasPendingException(), "valid sizing constructs a map");
        if (interp.HasPendingException()) { interp.ClearPendingException(); continue; }
        Check(Call(interp, map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {str("absent")}).l == nullptr,
              "empty lookup succeeds");
        for (int i = 0; i < 48; ++i) {
            const std::string key = "key" + std::to_string(i);
            Call(interp, map, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                 {str(key.c_str()), str("value")});
        }
        Check(Call(interp, map, "size", "()I").i == 48, "entries survive resize");
        Check(IsString(Call(interp, map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {str("key47")}), "value"),
              "last inserted value is readable");
        Check(IsString(Call(interp, map, "putIfAbsent", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;",
                            {str("key0"), str("replacement")}), "value"), "putIfAbsent retains existing value");
        Check(IsString(Call(interp, map, "remove", "(Ljava/lang/Object;)Ljava/lang/Object;", {str("key0")}), "value"),
              "remove returns existing value");
        Check(Call(interp, map, "get", "(Ljava/lang/Object;)Ljava/lang/Object;", {str("key0")}).l == nullptr,
              "removed key is absent");
        Call(interp, map, "clear", "()V");
        Check(Call(interp, map, "size", "()I").i == 0, "clear empties map");
    }
    const Sizing invalid[] = {{-1, 0.75f, 1}, {0, 0.0f, 1}, {0, -0.0f, 1}, {0, -1.0f, 1},
        {0, std::numeric_limits<float>::quiet_NaN(), 1}, {0, 0.75f, 0}, {0, 0.75f, -1}};
    for (const auto& sizing : invalid) {
        DexObject* map = linker.AllocObject(klass);
        DexValue args[] = {DexValue::Ref(map), DexValue::Int(sizing.capacity),
                          DexValue::Float(sizing.factor), DexValue::Int(sizing.concurrency)};
        interp.Execute(ctor, args, 4);
        DexObject* ex = interp.pending_exception();
        Check(ex && ex->clazz && std::strcmp(ex->clazz->descriptor, "Ljava/lang/IllegalArgumentException;") == 0,
              "invalid sizing throws IllegalArgumentException");
        interp.ClearPendingException();
    }
    DexObject* map = linker.AllocObject(klass);
    DexValue args[] = {DexValue::Ref(map), DexValue::Int(std::numeric_limits<int>::max()),
                      DexValue::Float(0.75f), DexValue::Int(1)};
    interp.Execute(ctor, args, 4);
    DexObject* ex = interp.pending_exception();
    Check(ex && ex->clazz && std::strcmp(ex->clazz->descriptor, "Ljava/lang/OutOfMemoryError;") == 0,
          "unrepresentable backing capacity fails without overflow");
    interp.ClearPendingException();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
