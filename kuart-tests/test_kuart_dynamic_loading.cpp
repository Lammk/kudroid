#include "kudroid/framework_dex_bytes.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/Interpreter.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
std::vector<uint8_t> Read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void RejectMalformed(const std::vector<uint8_t>& valid) {
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    for (size_t length = 0; length < valid.size(); ++length) {
        std::vector<uint8_t> truncated(valid.begin(), valid.begin() + length);
        Check(!linker.AddOwnedDexFiles({truncated}, &error), "truncated DEX rejected");
        Check(linker.NumDexFiles() == 0, "truncation registers no DEX");
    }
    auto corrupt = valid;
    corrupt[12] ^= 1;
    Check(!linker.AddOwnedDexFiles({valid, corrupt}, &error), "checksum failure rejects transaction");
    Check(linker.NumDexFiles() == 0, "invalid second buffer rolls back first");
    for (size_t field = 32; field < 112; field += 4) {
        corrupt = valid;
        std::fill(corrupt.begin() + field, corrupt.begin() + field + 4, 0xff);
        Check(!linker.AddOwnedDexFiles({corrupt}, &error), "invalid header field rejected");
    }
    art::DexFile::Header header;
    std::memcpy(&header, valid.data(), sizeof(header));
    for (size_t offset : {size_t(header.string_ids_off_), size_t(header.type_ids_off_),
                          size_t(header.map_off_)}) {
        corrupt = valid;
        std::fill(corrupt.begin() + offset, corrupt.begin() + offset + 4, 0xff);
        uint32_t checksum = art::DexFile::CalculateChecksum(corrupt.data(), corrupt.size());
        std::memcpy(corrupt.data() + 8, &checksum, sizeof(checksum));
        Check(!linker.AddOwnedDexFiles({corrupt}, &error), "malformed DEX with valid checksum rejected");
    }
    Check(linker.NumDexFiles() == 0, "malformed headers publish nothing");
    Check(linker.AddOwnedDexFiles({valid}, &error), "valid DEX accepted after malformed inputs");
    Check(linker.NumDexFiles() == 1, "one valid registration");
    for (size_t offset = 0; offset < valid.size(); ++offset) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            corrupt = valid;
            corrupt[offset] ^= uint8_t(1u << bit);
            uint32_t checksum = art::DexFile::CalculateChecksum(corrupt.data(), corrupt.size());
            std::memcpy(corrupt.data() + 8, &checksum, sizeof(checksum));
            kudroid::kuart::DexClassLinker probe;
            bool accepted = probe.AddOwnedDexFiles({corrupt}, &error);
            Check(probe.NumDexFiles() == (accepted ? 1u : 0u), "mutated input publishes only on success");
        }
    }
}
}

int main(int argc, char** argv) {
    auto heap = Read(std::string(KUART_DYNAMIC_FIXTURES) + "/DynamicPayloadHeap.dex");
    if (heap.size() < 112) return 1;
    RejectMalformed(heap);
    if (argc > 1 && std::string(argv[1]) == "--validation-only") {
        std::printf("DEX validation: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    }
    char task_temp[] = "/tmp/kudroid-dynamic-test-XXXXXX";
    if (mkdtemp(task_temp) == nullptr) return 1;
    kudroid::VFSPathRemapper::getInstance(false).setDocumentsDirectory(task_temp);
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    auto tests = Read(KUART_DYNAMIC_TEST_DEX);
    std::vector<uint8_t> framework;
    if (argc > 2 && std::string(argv[1]) == "--framework") {
        framework = Read(argv[2]);
        if (framework.empty()) return 1;
    }
    if (!linker.AddDexFile(framework.empty() ? g_framework_dex_bytes : framework.data(),
                          framework.empty() ? g_framework_dex_size : framework.size(),
                          "framework.dex", &error, true) ||
        tests.empty() || !linker.AddDexFile(tests.data(), tests.size(), "tests.dex", &error)) return 1;
    kudroid::kuart::Interpreter interp(&linker);
    kudroid::kuart::DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(2000000000);
    JNIEnv* env = jni.env();
    jclass byte_array = env->FindClass("[B");
    jobjectArray inputs = env->NewObjectArray(6, byte_array, nullptr);
    const char* names[] = {"Heap", "Direct", "View", "Dependency", "Caller", "Rollback"};
    for (int i = 0; i < 6; ++i) {
        auto bytes = Read(std::string(KUART_DYNAMIC_FIXTURES) + "/DynamicPayload" + names[i] + ".dex");
        if (bytes.empty()) return 1;
        jbyteArray input = env->NewByteArray(bytes.size());
        env->SetByteArrayRegion(input, 0, bytes.size(), reinterpret_cast<const jbyte*>(bytes.data()));
        env->SetObjectArrayElement(inputs, i, input);
        env->DeleteLocalRef(input);
    }
    jclass klass = env->FindClass("DynamicLoadingTests");
    jmethodID run = klass ? env->GetStaticMethodID(klass, "run", "([[B)I") : nullptr;
    if (!run || env->ExceptionCheck()) return 1;
    jvalue argument{};
    argument.l = inputs;
    int java_checks = env->CallStaticIntMethodA(klass, run, &argument);
    if (env->ExceptionCheck()) {
        std::printf("FAIL DynamicLoadingTests: %s\n%s\n", interp.DescribePendingException().c_str(),
                    interp.pending_exception_trace().c_str());
        ++failures;
    } else Check(java_checks > 0, "framework regressions executed");
    std::filesystem::remove_all(task_temp);
    std::printf("Dynamic loading: %d native + %d framework checks, %d failures\n", checks, java_checks, failures);
    return failures ? 1 : 0;
}
