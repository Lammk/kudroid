#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
    kudroid::kuart::DexClassLinker linker;
    std::string error;
    if (bytes.empty() || !linker.AddDexFile(bytes.data(), bytes.size(), argv[1], &error)) return 2;
    kudroid::kuart::Interpreter interpreter(&linker);
    auto* stringClass = linker.FindClass("Ljava/lang/String;");
    auto* string = linker.NewString("shared-value");
    kudroid::kuart::DexMethod method;
    method.declaring_class = stringClass;
    method.name = "intern";
    method.signature = "()Ljava/lang/String;";
    method.access_flags = art::kAccNative;
    kudroid::kuart::DexValue arg = kudroid::kuart::DexValue::Ref(string);
    std::promise<void> entering;
    auto entered = entering.get_future();
    std::future<kudroid::kuart::DexValue> result;
    bool serialized = false;
    {
        kudroid::kuart::VmLockGuard owner;
        result = std::async(std::launch::async, [&] {
            entering.set_value();
            return interpreter.Execute(&method, &arg, 1);
        });
        if (entered.wait_for(std::chrono::seconds(2)) != std::future_status::ready) return 1;
        serialized = result.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout;
    }
    if (result.wait_for(std::chrono::seconds(2)) != std::future_status::ready) return 1;
    bool valid = result.get().l != nullptr;
    std::printf("Builtin entry: serialized=%d, completed after release=%d\n", serialized, valid);
    return serialized && valid ? 0 : 1;
}
