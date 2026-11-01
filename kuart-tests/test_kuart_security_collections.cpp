#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    std::vector<uint8_t> framework;
    if (argc > 2) {
        std::ifstream input(argv[2], std::ios::binary);
        framework.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (framework.empty()) return 1;
    }
    if (!linker.AddDexFile(framework.empty() ? g_framework_dex_bytes : framework.data(),
                          framework.empty() ? g_framework_dex_size : framework.size(), "framework.dex", &error)) return 1;
    std::ifstream file(KUART_SECURITY_COLLECTIONS_DEX, std::ios::binary);
    std::vector<uint8_t> dex((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (dex.empty() || !linker.AddDexFile(dex.data(), dex.size(), "regressions.dex", &error)) {
        std::printf("FAIL loading test DEX: %s\n", error.c_str()); return 1;
    }
    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000000000);
    JNIEnv* env = jni.env();
    const char* test = argc > 1 ? argv[1] : "SecurityStartupTests";
    jclass klass = env->FindClass(test);
    jmethodID run = klass ? env->GetStaticMethodID(klass, "run", "()I") : nullptr;
    if (!run || env->ExceptionCheck()) { std::printf("FAIL resolving %s\n", test); return 1; }
    int checks = env->CallStaticIntMethod(klass, run);
    if (env->ExceptionCheck()) {
        std::printf("FAIL %s: %s\n%s\n", test, interp.DescribePendingException().c_str(),
                    interp.pending_exception_trace().c_str());
        return 1;
    }
    std::printf("%s: %d checks, 0 failures\n", test, checks);
    return checks > 0 ? 0 : 1;
}
