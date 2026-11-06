#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/JavaCanvasRenderer.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;

static void configureSurface(JNIEnv*, jclass, jboolean ready, jint width, jint height) {
    g_metalLayer = ready ? reinterpret_cast<void*>(1) : nullptr;
    g_metalLayerWidth = width;
    g_metalLayerHeight = height;
    kudroid::JavaCanvasRenderer::getInstance().init(width, height);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    using namespace kudroid::kuart;
    DexClassLinker linker;
    std::vector<std::vector<uint8_t>> files;
    std::string error;
    for (int i = 1; i < argc; ++i) {
        std::ifstream file(argv[i], std::ios::binary);
        files.emplace_back(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (files.back().empty() || !linker.AddDexFile(files.back().data(), files.back().size(), argv[i], &error)) {
            std::printf("FAIL loading DEX: %s\n", error.c_str());
            return 1;
        }
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(10000000);
    JNIEnv* env = jni.env();
    jclass api = env->FindClass("android/app/SurfaceLifecycleTests");
    char name[] = "configureSurface";
    char signature[] = "(ZII)V";
    JNINativeMethod native{name, signature, reinterpret_cast<void*>(&configureSurface)};
    if (!api || env->RegisterNatives(api, &native, 1) != JNI_OK) return 1;
    auto* klass = linker.FindClass("Landroid/app/SurfaceLifecycleTests;");
    if (!klass || !interp.EnsureInitialized(klass)) return 1;
    auto* method = klass->FindDirectMethod("run", "()Ljava/lang/String;");
    if (!method) return 1;
    DexValue value = interp.Execute(method, nullptr, 0);
    if (interp.HasPendingException() || !value.l) {
        std::printf("FAIL guest exception: %s\n", interp.DescribePendingException().c_str());
        return 1;
    }
    std::istringstream lines(static_cast<DexString*>(value.l)->utf8);
    int checks = 0, failures = 0;
    for (std::string line; std::getline(lines, line);) {
        ++checks;
        failures += line.rfind("FAIL", 0) == 0;
        std::puts(line.c_str());
    }
    std::printf("Surface lifecycle: %d checks, %d failures\n", checks, failures);
    return failures || checks != 19 ? 1 : 0;
}
