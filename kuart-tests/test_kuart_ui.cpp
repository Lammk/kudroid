// UI contracts exercised through Android bytecode and the guest interpreter.
#include "kudroid/framework_dex_bytes.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/DexJniEnv.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/kuart/VmLock.h"
#include "kudroid/platform/JavaCanvasRenderer.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

using namespace kudroid::kuart;

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    DexClassLinker linker;
    std::string error;
    std::ifstream file(KUART_UI_TEST_DEX, std::ios::binary);
    std::vector<uint8_t> dex((std::istreambuf_iterator<char>(file)), {});
    if (!linker.AddDexFile(g_framework_dex_bytes, g_framework_dex_size, "framework.dex", &error)
        || dex.empty() || !linker.AddDexFile(dex.data(), dex.size(), "tests.dex", &error)) {
        std::printf("FAIL loading DEX: %s\n", error.c_str());
        return 1;
    }
    Interpreter interp(&linker);
    DexJniEnv jni(&linker, &interp);
    interp.set_jni_env(&jni);
    interp.set_instruction_limit(8000ull * 1000ull * 1000ull);
    DexClass* klass = linker.FindClass("LUiAbiTests;");
    if (!klass || !interp.EnsureInitialized(klass)) return 1;
    const bool bench = argc >= 2;
    const bool traversal = argc == 3 && std::string(argv[1]) == "--traversal";
    const bool parked = argc == 3 && std::string(argv[1]) == "--waiters";
    const bool drawing = argc == 3 && std::string(argv[1]) == "--draw";
    DexMethod* method = klass->FindDirectMethod(bench ? (drawing ? "drawBench" : traversal ? "traversalBench" : "viewBench") : "run",
                                              bench ? "(I)I" : "()Ljava/lang/String;");
    if (!method) return 1;
    DexValue count{};
    count.i = bench ? std::atoi(argv[(traversal || parked || drawing) ? 2 : 1]) : 0;
    DexObject monitors[12];
    std::vector<std::thread> waiters;
    if (parked) {
        for (auto& monitor : monitors) {
            waiters.emplace_back([&monitor] {
                VmLockGuard guard;
                Monitor::Enter(&monitor);
                Monitor::Wait(&monitor, 0, 0);
                Monitor::Exit(&monitor);
            });
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (g_monitorWaiters.load() != 12 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (g_monitorWaiters.load() != 12) std::abort();
    }
    const auto start = std::chrono::steady_clock::now();
    const size_t heap_before = linker.heap().BytesAllocated();
    DexValue result = interp.Execute(method, bench ? &count : nullptr, bench ? 1 : 0);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    if (parked) {
        for (auto& monitor : monitors) {
            VmLockGuard guard;
            Monitor::Enter(&monitor);
            Monitor::Notify(&monitor, true);
            Monitor::Exit(&monitor);
        }
        for (auto& waiter : waiters) waiter.join();
    }
    if (interp.HasPendingException()) {
        std::printf("FAIL guest exception: %s\n", interp.DescribePendingException().c_str());
        return 1;
    }
    if (bench) {
        std::printf("%d %s: %.3f ms, width sum %d, parked waiters %d\n", count.i,
                    drawing ? "draws" : traversal ? "traversals" : "views", ms, result.i, parked ? 12 : 0);
        std::printf("Guest heap allocated: %zu bytes\n", linker.heap().BytesAllocated() - heap_before);
        return count.i > 0 && result.i == count.i * 200 ? 0 : 1;
    }
    auto& renderer = kudroid::JavaCanvasRenderer::getInstance();
    renderer.init(2, 2);
    uint32_t source[] = {0xff123456};
    renderer.drawColor(0x00000000);
    renderer.drawBitmap(source, 1, 1, 0, 0, 128, 0xffff8800, 5);
    bool native_color = renderer.getBuffer()[0] == 0x80004480 && source[0] == 0xff123456;
    std::printf("%s native bitmap filter preserves source and composites alpha\n", native_color ? "OK  " : "FAIL");
    renderer.drawColor(0x00000000);
    renderer.drawBitmapRegion(source, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 255, 0xff00ff00, 5);
    bool native_region = renderer.getBuffer()[3] == 0xff00ff00;
    std::printf("%s native bitmap region applies filter across scaled destination\n", native_region ? "OK  " : "FAIL");
    std::istringstream lines(result.l ? static_cast<DexString*>(result.l)->utf8 : "");
    int checks = 2, failures = !native_color + !native_region;
    std::string line;
    while (std::getline(lines, line)) {
        ++checks;
        if (line.rfind("FAIL", 0) == 0) ++failures;
        std::printf("%s\n", line.c_str());
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return checks > 0 && failures == 0 ? 0 : 1;
}
