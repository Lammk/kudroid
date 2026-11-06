#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) return 2;
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    std::vector<std::vector<uint8_t>> files;
    for (int i = 1; i < (argc == 4 ? 3 : 2); ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        files.emplace_back(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        const auto& bytes = files.back();
        if (bytes.empty() || !linker.AddDexFile(bytes.data(), bytes.size(), argv[i], &error)) {
            std::printf("FAIL loading DEX: %s\n", error.c_str());
            return 1;
        }
    }
    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(100000000);
    JNIEnv* env = jni.env();
    jclass api = env->FindClass("android/net/ssl/SSLSockets");
    jmethodID supported = api ? env->GetStaticMethodID(api, "isSupportedSocket",
                                                       "(Ljavax/net/ssl/SSLSocket;)Z") : nullptr;
    if (supported) env->CallStaticBooleanMethod(api, supported, nullptr);
    if (!supported || env->ExceptionCheck()) {
        std::printf("FAIL Android TLS provider detection: %s\n",
                    interp.DescribePendingException().c_str());
        return 1;
    }
    std::puts("PASS Android TLS API resolves and executes through JNI");
    if (argc == 2) return 0;
    jclass fixture = env->FindClass("AndroidTlsTests");
    jmethodID run = fixture ? env->GetStaticMethodID(fixture, "run", "(I)I") : nullptr;
    if (!run || env->ExceptionCheck()) return 1;
    int checks = env->CallStaticIntMethod(fixture, run, std::atoi(argv[3]));
    if (env->ExceptionCheck()) {
        std::printf("FAIL TLS regression: %s\n%s\n", interp.DescribePendingException().c_str(),
                    interp.pending_exception_trace().c_str());
        return 1;
    }
    std::printf("Android TLS: %d checks, 0 failures\n", checks);
    return checks > 0 ? 0 : 1;
}
