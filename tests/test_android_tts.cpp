#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (bytes.empty() || !linker.AddDexFile(bytes.data(), bytes.size(), argv[1], &error)) return 2;
    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    JNIEnv* env = jni.env();
    jclass api = env->FindClass("android/speech/tts/TextToSpeech");
    jmethodID ctor = env->GetMethodID(api, "<init>", "()V");
    jobject tts = ctor ? env->NewObject(api, ctor) : nullptr;
    int checks = 0, failures = 0;
    auto check = [&](bool value, const char* name) {
        ++checks;
        failures += !value;
        std::printf("%s %s\n", value ? "PASS" : "FAIL", name);
        env->ExceptionClear();
    };
    jclass localeClass = env->FindClass("java/util/Locale");
    jfieldID english = env->GetStaticFieldID(localeClass, "ENGLISH", "Ljava/util/Locale;");
    jobject locale = english ? env->GetStaticObjectField(localeClass, english) : nullptr;
    check(locale && !env->ExceptionCheck(), "locale argument is available");
    for (const char* name : {"setLanguage", "isLanguageAvailable"}) {
        jmethodID method = env->GetMethodID(api, name, "(Ljava/util/Locale;)I");
        jint result = method ? env->CallIntMethod(tts, method, nullptr) : 0;
        check(method && !env->ExceptionCheck() && result == -2, name);
        result = method ? env->CallIntMethod(tts, method, locale) : 0;
        check(method && !env->ExceptionCheck() && result == -2, "locale remains unsupported without a service");
    }
    jmethodID stop = env->GetMethodID(api, "stop", "()I");
    check(stop && env->CallIntMethod(tts, stop) == -1 && !env->ExceptionCheck(), "stop unavailable");
    jmethodID shutdown = env->GetMethodID(api, "shutdown", "()V");
    if (shutdown) env->CallVoidMethod(tts, shutdown);
    check(shutdown && !env->ExceptionCheck(), "shutdown");
    check(stop && env->CallIntMethod(tts, stop) == -1 && !env->ExceptionCheck(), "stop after shutdown");
    std::printf("TTS: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
