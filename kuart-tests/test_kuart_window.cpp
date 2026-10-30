// Window traversal and appearance delivery through the guest framework.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/platform/ThermalShim.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using kudroid::kuart::DexClass;
using kudroid::kuart::DexClassLinker;
using kudroid::kuart::DexJniEnv;
using kudroid::kuart::DexMethod;
using kudroid::kuart::DexString;
using kudroid::kuart::DexValue;
using kudroid::kuart::Interpreter;

int g_checks = 0;
int g_failures = 0;

void Fail(const std::string& what) {
    std::printf("  FAIL %s\n", what.c_str());
    ++g_checks;
    ++g_failures;
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("=== KuART window traversal and appearance ===\n");
    kudroid_set_safe_area_insets(4, 44, 6, 34);
    DexClassLinker linker;
    std::string error;
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)) {
        std::printf("  FAIL AddDexFile(framework.dex): %s\n", error.c_str());
        return 1;
    }
    const std::vector<uint8_t> testDex = ReadFile(KUART_WINDOW_TEST_DEX);
    if (testDex.empty() || !linker.AddDexFile(testDex.data(), testDex.size(), "tests.dex", &error)) {
        std::printf("  FAIL AddDexFile(tests.dex): %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(8000ull * 1000ull * 1000ull);

    for (const char* name : {"LWindowTraversalTests;", "LThemeBridgeTests;", "LBackNavigationTests;"}) {
        std::printf("[%s]\n", name);
        DexClass* klass = linker.FindClass(name);
        if (klass == nullptr || !interp.EnsureInitialized(klass)) {
            Fail(std::string("class not found: ") + name);
            continue;
        }
        DexMethod* method = klass->FindDirectMethod("run", "()Ljava/lang/String;");
        if (method == nullptr) {
            Fail(std::string(name) + ".run()Ljava/lang/String; not found");
            continue;
        }
        interp.ClearPendingException();
        const DexValue v = interp.Execute(method, nullptr, 0);
        if (interp.HasPendingException()) {
            Fail(std::string(name) + " threw " + interp.DescribePendingException());
            interp.ClearPendingException();
            continue;
        }
        std::istringstream lines(v.l != nullptr ? static_cast<DexString*>(v.l)->utf8 : "");
        std::string line;
        while (std::getline(lines, line)) {
            ++g_checks;
            if (line.rfind("FAIL", 0) == 0) ++g_failures;
            std::printf("  %s\n", line.c_str());
        }
    }
    DexClass* thermalClass = linker.FindClass("LWindowTraversalTests;");
    DexMethod* thermalMethod = thermalClass ? thermalClass->FindDirectMethod("thermalProbe", "(I)Ljava/lang/String;") : nullptr;
    if (!thermalMethod) {
        Fail("thermal probe method is missing");
    } else {
        for (int status : {3, 1, -1}) {
            kudroid_set_thermal_status(status);
            const DexValue argument = DexValue::Int(status);
            interp.ClearPendingException();
            const DexValue report = interp.Execute(thermalMethod, &argument, 1);
            if (interp.HasPendingException()) {
                Fail("thermal probe threw " + interp.DescribePendingException());
                interp.ClearPendingException();
                continue;
            }
            std::istringstream lines(report.l ? static_cast<DexString*>(report.l)->utf8 : "");
            std::string line;
            while (std::getline(lines, line)) {
                ++g_checks;
                if (line.rfind("FAIL", 0) == 0) ++g_failures;
                std::printf("  %s\n", line.c_str());
            }
        }
    }
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
