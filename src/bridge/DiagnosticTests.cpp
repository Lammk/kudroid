// DiagnosticTests.cpp — the self-test suite (ELF/SO execution tests, JNI
// mass-call test, GPU shader tests), app deletion and info, APK loading and
// DEX translation. Split out of kudroid_bridge.cpp.
#include "BridgeState.h"
#include "BridgeShared.h"
#include "GuestLibrary.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/BionicShim.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/DeviceProfile.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/PermissionManager.h"
#include "kudroid/Log.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
void installCrashHandlers(void);
extern "C" void kudroid_gpu_cleanup_on_test_exit(void);

extern "C" int kudroid_self_test(void) {
    fprintf(stderr, "[kudroid_core] Self-test starting...\n");
    fprintf(stderr, "[kudroid_core] Creating ElfLoader with dummy path '/nonexistent'...\n");

    try {
        kudroid::ElfLoader loader("/nonexistent");

        fprintf(stderr, "[kudroid_core] ElfLoader constructed OK.\n");
        fprintf(stderr, "[kudroid_core] isLoaded() = %s\n", loader.isLoaded() ? "true" : "false");
        fprintf(stderr, "[kudroid_core] entryPoint() = 0x%llx\n", (unsigned long long)loader.entryPoint());
        fprintf(stderr, "[kudroid_core] segments count = %zu\n", loader.segments().size());

        fprintf(stderr, "[kudroid_core] Calling parse()...\n");
        bool ok = loader.parse();
        fprintf(stderr, "[kudroid_core] parse() returned %s\n", ok ? "true" : "false");

        fprintf(stderr, "[kudroid_core] Self-test PASSED.\n");
        (void)loader;
        return 0;
    } catch (...) {
        fprintf(stderr, "[kudroid_core] Self-test FAILED: exception thrown!\n");
        return -1;
    }
}

extern "C" const char* kudroid_self_test_log(void) {
    std::string log;
    appendTestHeader(log, "Self-Test", "/nonexistent");
    log += "[kudroid_core] Phase: construct ElfLoader\n";
    log += "[kudroid_core] Creating ElfLoader with dummy path '/nonexistent'...\n";

    try {
        kudroid::ElfLoader loader("/nonexistent");

        log += "[kudroid_core] ElfLoader constructed OK.\n";

        char buf[256];
        snprintf(buf, sizeof(buf), "[kudroid_core] isLoaded() = %s\n", loader.isLoaded() ? "true" : "false");
        log += buf;

        snprintf(buf, sizeof(buf), "[kudroid_core] entryPoint() = 0x%llx\n", (unsigned long long)loader.entryPoint());
        log += buf;

        snprintf(buf, sizeof(buf), "[kudroid_core] segments count = %zu\n", loader.segments().size());
        log += buf;

        log += "[kudroid_core] Phase: parse()\n";
        bool ok = loader.parse();

        snprintf(buf, sizeof(buf), "[kudroid_core] parse() returned %s\n", ok ? "true" : "false");
        log += buf;

        log += "[kudroid_core] Self-test PASSED.\n";
        (void)loader;

        writeLogFile("kudroid_selftest.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) {
            memcpy(result, log.c_str(), log.size() + 1);
        }
        return result;
    } catch (...) {
        log += "[kudroid_core] Self-test FAILED: exception thrown!\n";

        writeLogFile("kudroid_selftest.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) {
            memcpy(result, log.c_str(), log.size() + 1);
        }
        return result;
    }
}

extern "C" const char* kudroid_load_elf(const char* path) {
    std::string log;
    appendTestHeader(log, "ELF Load", path);
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Loading ELF: %s\n", path);
    log += buf;

    try {
        kudroid::ElfLoader loader(path);

        log += "[kudroid_core] Phase: parse ELF headers\n";
        bool ok = loader.parse();

        if (!ok) {
            snprintf(buf, sizeof(buf), "[kudroid_core] PARSE FAILED: %s\n", loader.lastError());
            log += buf;
        } else {
            log += "[kudroid_core] ELF parsed successfully.\n";

            snprintf(buf, sizeof(buf), "[kudroid_core]   Entry point: 0x%llx\n", (unsigned long long)loader.entryPoint());
            log += buf;

            snprintf(buf, sizeof(buf), "[kudroid_core]   PT_LOAD segments: %zu\n", loader.segments().size());
            log += buf;

            int segNum = 0;
            for (const auto& seg : loader.segments()) {
                snprintf(buf, sizeof(buf),
                    "[kudroid_core]   [%d] vaddr=0x%llx offset=0x%llx filesz=%llu memsz=%llu flags=0x%x\n",
                    segNum++,
                    (unsigned long long)seg.vaddr,
                    (unsigned long long)seg.offset,
                    (unsigned long long)seg.filesz,
                    (unsigned long long)seg.memsz,
                    seg.flags);
                log += buf;
            }

            // attempt mapping (currently stubbed)
            log += "[kudroid_core] Phase: map PT_LOAD segments\n";
            if (loader.map()) {
                log += "[kudroid_core] Map OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Map failed: %s\n", loader.lastError());
                log += buf;
            }

            // attempt relocation (currently stubbed)
            log += "[kudroid_core] Phase: resolve relocations/imports\n";
            if (loader.relocate()) {
                log += "[kudroid_core] Relocate OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Relocate failed: %s\n", loader.lastError());
                log += buf;
            }
        }

        log += "[kudroid_core] Load complete.\n";

        writeLogFile("kudroid_load.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    } catch (...) {
        log += "[kudroid_core] EXCEPTION during load!\n";
        writeLogFile("kudroid_load.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }
}

extern "C" const char* kudroid_execution_test(const char* path) {
    std::string log;
    appendTestHeader(log, "ELF Execution", path);
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Execution test for: %s\n", path);
    log += buf;

    // Refuse native execution when JIT is disabled: executing PROT_EXEC pages
    // without dynamic code signing terminates the process abruptly.
    if (!kudroid_jit_available()) {
        log += "[kudroid_core] ABORT: JIT is Disabled — cannot execute native code.\n";
        log += "[kudroid_core] Enable JIT in LiveContainer and retry.\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    try {
        kudroid::ElfLoader loader(path);

        if (!loader.parse()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] PARSE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: parse -> OK\n";

        if (!loader.map()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] MAP FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: map -> OK\n";

        if (!loader.relocate()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] RELOCATE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Phase: relocate/import binding -> OK\n";
        log += "[kudroid_core] Phase: invoke exported kudroid_add(40, 20)\n";

        // Snapshot logs for crash handler prior to executing JIT code.
        mirrorCrash(log);

        std::string execResult = loader.testExecution();
        log += execResult;
        log += "\n";

        writeLogFile("kudroid_exec.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    } catch (...) {
        log += "[kudroid_core] EXCEPTION during execution test!\n";
        writeLogFile("kudroid_exec.txt", log);
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }
}

extern "C" const char* kudroid_bionic_execution_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Bionic Shim Execution", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_core] ABORT: JIT is Disabled\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_core] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_core] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_core] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_core] ELF mapped and Bionic imports bound.\n";
            void* address = loader.getSymbolAddress("kudroid_bionic_test");
            if (!address) {
                log += "[kudroid_core] SYMBOL FAILED: kudroid_bionic_test not found\n";
            } else {
                log += "[kudroid_core] Running kudroid_bionic_test()...\n";
                const char* shimTrace = kudroid::bionic_shim_trace();
                if (shimTrace && *shimTrace) {
                    log += "[kudroid_core] Bionic trace before call:\n";
                    log += shimTrace;
                }
                mirrorCrash(log);
                using TestFunction = int (*)();
                const int result = reinterpret_cast<TestFunction>(address)();
                log += "[kudroid_core] BIONIC TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)\n" : " (FAILED)\n");
            }
        }
    }

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_core] Bionic trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_bionic_test.txt", log);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_multi_elf_test(const char* consumerPath,
                                               const char* providerPath) {
    std::string log;
    appendTestHeader(log, "Multi-ELF Dependency Resolution", consumerPath);
    kudroid::bionic_shim_reset_trace();
    log += "[kudroid_core] Phase: create LibraryManager\n";

    if (!consumerPath || !providerPath) {
        log += "[kudroid_core] ERROR: null ELF path\n";
    } else {
        kudroid::LibraryManager manager;
        log += "[kudroid_core] Phase: parse DT_NEEDED\n";
        const auto dependencies = kudroid::parse_elf_dependencies(consumerPath);
        log += "[kudroid_core] DT_NEEDED count: " +
               std::to_string(dependencies.size()) + "\n";
        for (const auto& dependency : dependencies) {
            log += "[kudroid_core]   dependency: " + dependency + "\n";
        }

        log += "[kudroid_core] Phase: load primary ELF recursively\n";
        if (!manager.loadRecursive(consumerPath)) {
            log += "[kudroid_core] PRIMARY LOAD FAILED: " +
                   manager.lastError() + "\n";
        } else {
            log += "[kudroid_core] Primary ELF load OK\n";
        }

        log += "[kudroid_core] Phase: load sibling ELF recursively\n";
        if (!manager.loadRecursive(providerPath)) {
            log += "[kudroid_core] SIBLING LOAD FAILED: " +
                   manager.lastError() + "\n";
        } else {
            log += "[kudroid_core] Sibling ELF load OK\n";
        }

        const std::size_t loadedCount = manager.libraries().size();
        log += "[kudroid_core] Loaded library count: " +
               std::to_string(loadedCount) + "\n";
        for (const auto& library : manager.libraries()) {
            log += "[kudroid_core]   loaded: " + library.first + "\n";
        }

        log += "[kudroid_core] Phase: duplicate-load prevention\n";
        const bool duplicateLoad = manager.loadRecursive(consumerPath);
        log += duplicateLoad && manager.libraries().size() == loadedCount
            ? "[kudroid_core] Duplicate load skipped successfully\n"
            : "[kudroid_core] Duplicate load check failed\n";

        log += "[kudroid_core] Phase: global symbol resolution\n";
        void* symbol = manager.resolveGlobalSymbol("kudroid_dependency_value");
        log += symbol
            ? "[kudroid_core] Global symbol kudroid_dependency_value resolved from provider\n"
            : "[kudroid_core] Global symbol kudroid_dependency_value NOT resolved\n";

        log += "[kudroid_core] Phase: execute consumer -> provider call\n";
        void* consumerSymbol = manager.resolveGlobalSymbol("kudroid_multi_elf_test");
        if (!consumerSymbol) {
            log += "[kudroid_core] Consumer symbol kudroid_multi_elf_test NOT resolved\n";
        } else if (!kudroid_jit_available()) {
            log += "[kudroid_core] Consumer execution skipped: JIT Disabled\n";
        } else {
            const char* trace = kudroid::bionic_shim_trace();
            if (trace && *trace) {
                log += "[kudroid_core] Import trace before execution:\n";
                log += trace;
            }
            mirrorCrash(log);
            using MultiElfFunction = int (*)();
            const int result = reinterpret_cast<MultiElfFunction>(consumerSymbol)();
            log += "[kudroid_core] Consumer returned: " + std::to_string(result) + "\n";
            log += result == 42
                ? "[kudroid_core] MULTI-ELF TEST RESULT: SUCCESS (35 + 7 = 42)\n"
                : "[kudroid_core] MULTI-ELF TEST RESULT: FAILED (expected 42)\n";
        }
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "[kudroid_core] Bionic/global binding trace:\n";
        log += trace;
    }

    writeLogFile("kudroid_multi_elf_test.txt", log);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" void kudroid_gpu_cleanup_on_test_exit(void);

extern "C" const char* kudroid_run_so_test(const char* soPath, const char* entrypoint) {
    std::string log;
    appendTestHeader(log, "Dynamic Remote .so Test Execution", soPath);
    kudroid::bionic_shim_reset_trace();
    kudroid_gpu_cleanup_on_test_exit();

    if (!soPath) {
        log += "❌ ERROR: null SO path provided\n";
        return strdup(log.c_str());
    }

    log += "[kudroid_runner] Loading library: " + std::string(soPath) + "\n";
    kudroid::LibraryManager manager;

    if (!manager.loadRecursive(soPath)) {
        log += "❌ LOAD FAILED: " + manager.lastError() + "\n";
        const char* trace = kudroid::bionic_shim_trace();
        if (trace && *trace) {
            log += "\n[kudroid_runner] Binding trace during failure:\n";
            log += trace;
        }
        return strdup(log.c_str());
    }
    log += "✔ Library loaded successfully with " + std::to_string(manager.libraries().size()) + " dependencies resolved\n";

    // Locate entry point
    std::vector<std::string> candidateNames;
    if (entrypoint && strlen(entrypoint) > 0) {
        candidateNames.push_back(entrypoint);
    }
    candidateNames.push_back("kudroid_test_main");
    candidateNames.push_back("kudroid_main");
    candidateNames.push_back("test_main");
    candidateNames.push_back("main");

    void* symbol = nullptr;
    std::string foundName;
    for (const auto& name : candidateNames) {
        symbol = manager.resolveGlobalSymbol(name.c_str());
        if (symbol) {
            foundName = name;
            break;
        }
    }

    if (!symbol) {
        log += "❌ ERROR: No recognized entrypoint symbol found! Candidates tried:\n";
        for (const auto& name : candidateNames) log += "  - " + name + "\n";
        return strdup(log.c_str());
    }
    log += "✔ Entrypoint resolved: '" + foundName + "' at " + std::to_string(reinterpret_cast<uintptr_t>(symbol)) + "\n";

    log += "🚀 Executing test function in sandbox...\n\n";
    mirrorCrash(log);

    // Support 64-bit function signatures returning const char* or uintptr_t
    typedef const char* (*StringTestFn)();
    auto strFn = reinterpret_cast<StringTestFn>(symbol);
    const char* outputStr = strFn();

    if (outputStr && reinterpret_cast<uintptr_t>(outputStr) > 0x10000) {
        log += "=== OUTPUT FROM TEST .SO ===\n";
        log += std::string(outputStr) + "\n";
    } else {
        const int retCode = static_cast<int>(reinterpret_cast<intptr_t>(outputStr));
        log += "=== TEST STATUS RETURNED ===\n";
        log += "Exit Code: " + std::to_string(retCode) + (retCode == 0 ? " (SUCCESS ✔)\n" : " (FAILED ❌)\n");
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "\n[kudroid_runner] Bionic runtime shim trace:\n";
        log += trace;
    }

    kudroid_gpu_cleanup_on_test_exit();
    log += "\n🎉 Test execution finished.\n";
    return strdup(log.c_str());
}

extern "C" int kudroid_clear_app_cache(const char* package_name) {
    if (!package_name) return 0;

    // construct app cache directory path using VFS path remapper
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path cachePath = std::filesystem::path(androidRoot) / "data/data" / package_name / "cache";
    std::filesystem::path codeCachePath = std::filesystem::path(androidRoot) / "data/data" / package_name / "code_cache";

    std::error_code ec;
    int success = 1;
    if (std::filesystem::exists(cachePath, ec)) {
        if (std::filesystem::remove_all(cachePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    if (std::filesystem::exists(codeCachePath, ec)) {
        if (std::filesystem::remove_all(codeCachePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    return success;
}

extern "C" int kudroid_delete_app(const char* package_name);

// Uninstall app with step-by-step progress callback (UTF-8 phase description, percent 0-100).
// Runs synchronously on caller thread — Swift UI dispatches to main thread.
// Provides real-time UI progress feedback during large asset/file tree deletion.
typedef void (*kudroid_delete_progress_cb)(const char* phase, int percent, void* userdata);

namespace {
// ── [DEBUG UNINSTALL] Step-by-Step Uninstall Trace ───────────────────────────
// Tracks full progress of file removals to <Documents>/logs/kudroid_uninstall_debug.txt
// for diagnosis during large app deletions.
std::string g_uninstallDbg;

void uninstallDbg(const std::string& msg) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm tmv;
    localtime_r(&now.tv_sec, &tmv);
    char ts[40];
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03ld",
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, now.tv_nsec / 1000000);
    g_uninstallDbg += std::string("[") + ts + "] " + msg + "\n";
    if (g_logDir[0]) {
        const std::string path =
            std::string(g_logDir) + "/kudroid_uninstall_debug.txt";
        if (FILE* f = fopen(path.c_str(), "w")) {
            fwrite(g_uninstallDbg.data(), 1, g_uninstallDbg.size(), f);
            fclose(f);
            ::chmod(path.c_str(), 0644); // readable via AFC/USB file sharing like stderr.log
        }
    }
}

// Summarize file/directory status (existence, type, permissions) for early diagnostics.
std::string describePath(const std::filesystem::path& p) {
    std::error_code ec;
    const std::string raw = p.string();
    std::string s = raw;
    if (!std::filesystem::exists(p, ec)) {
        s += ec ? " [exists-err: " + ec.message() + "]" : " [MISSING]";
    } else {
        s += std::filesystem::is_directory(p, ec) ? " [dir]" : " [file]";
    }
    if (::access(raw.c_str(), W_OK) != 0)
        s += std::string(" [NOT-WRITABLE errno=") + std::strerror(errno) + "]";
    else
        s += " [writable]";
    return s;
}

// Fast top-level child directory pruning via atomic remove_all operations.
// Avoids deep recursive filesystem traversal overhead on large asset trees.
void removeTreeWithProgress(const std::filesystem::path& p,
                            const char* phase,
                            kudroid_delete_progress_cb cb, void* ud,
                            double basePct, double spanPct,
std::uint64_t /*unused parameter*/) {
    const auto nowMs = [] {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    };
    std::error_code ec;
    uninstallDbg("phase='" + std::string(phase) + "' path=" + describePath(p));
    if (!std::filesystem::exists(p, ec)) return;

    // Single file entry (rare).
    if (!std::filesystem::is_directory(p, ec)) {
        const std::int64_t t0 = nowMs();
        std::filesystem::remove(p, ec);
        uninstallDbg("  single-file removed in " + std::to_string(nowMs() - t0) +
                     "ms ec=" + (ec ? ec.message() : std::string("none")));
        return;
    }

    // List top-level directory children non-recursively.
    std::vector<std::filesystem::path> children;
    for (std::filesystem::directory_iterator cIt(p, ec), cEnd;
         cIt != cEnd && !ec; cIt.increment(ec)) {
        children.push_back(cIt->path());
    }
    if (ec) uninstallDbg("  listing FAILED: " + ec.message());

    const std::int64_t t0 = nowMs();
    int idx = 0;
    bool hadErrors = false;
    for (const auto& child : children) {
        const std::int64_t childStart = nowMs();
        std::error_code rmEc;
        const auto n = std::filesystem::remove_all(child, rmEc);
        const std::int64_t ms = nowMs() - childStart;
        if (rmEc) hadErrors = true;
        uninstallDbg("  [" + std::to_string(++idx) + "/" +
                     std::to_string(children.size()) + "] remove_all " +
                     child.filename().string() + " -> " + std::to_string(n) +
                     " entries in " + std::to_string(ms) + "ms" +
                     (rmEc ? " ec=" + rmEc.message() : "") +
                     (ms >= 2000 ? " \u26a0\ufe0f SLOW" : ""));
        if (cb && spanPct > 0 && !children.empty()) {
            const int pct = static_cast<int>(
                basePct + spanPct * static_cast<double>(idx) /
                              static_cast<double>(children.size()));
            cb(phase, pct, ud);
        }
    }
    // Remove root directory (now empty).
    std::error_code raEc;
    std::filesystem::remove_all(p, raEc);
    uninstallDbg("  phase done in " + std::to_string(nowMs() - t0) +
                 "ms children=" + std::to_string(children.size()) +
                 " residue=" +
                 (std::filesystem::exists(p, raEc) ? "YES(still-there!)" : "no") +
                 (hadErrors ? " HAD_ERRORS" : ""));
}
} // namespace

extern "C" int kudroid_delete_app_progress(const char* package_name,
                                           kudroid_delete_progress_cb cb,
                                           void* userdata) {
    g_uninstallDbg.clear();
    uninstallDbg(std::string("=== UNINSTALL START pkg=") +
                 (package_name ? package_name : "(null)") +
                 " cb=" + (cb ? "yes" : "NO") + " ===");
    if (!package_name) {
        uninstallDbg("ABORT: package_name null");
        return 0;
    }

    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    uninstallDbg("androidRoot=" + androidRoot);
    const std::filesystem::path appCodePath =
        std::filesystem::path(androidRoot) / "data/app" / package_name;
    const std::filesystem::path appDataPath =
        std::filesystem::path(androidRoot) / "data/data" / package_name;
    // External storage the app owns. /sdcard maps to <androidRoot>/sdcard, so an
    // uninstall that only removed data/data left the game's saves, obb and media
    // behind (stale caches then poisoned the next install).
    const std::filesystem::path extDataPath =
        std::filesystem::path(androidRoot) / "sdcard/Android/data" / package_name;
    const std::filesystem::path extObbPath =
        std::filesystem::path(androidRoot) / "sdcard/Android/obb" / package_name;
    const std::filesystem::path extMediaPath =
        std::filesystem::path(androidRoot) / "sdcard/Android/media" / package_name;
    const std::filesystem::path dalvikRoot =
        std::filesystem::path(androidRoot) / "data/dalvik-cache";
    uninstallDbg("code:   " + describePath(appCodePath));
    uninstallDbg("data:   " + describePath(appDataPath));
    uninstallDbg("ext:    " + describePath(extDataPath));
    uninstallDbg("obb:    " + describePath(extObbPath));
    uninstallDbg("media:  " + describePath(extMediaPath));
    uninstallDbg("dalvik: " + describePath(dalvikRoot));

    // Avoid recursive tree byte scanning; progress is tracked across top-level folders.
    std::error_code ec;
    // Prune any legacy dalvik-cache directory artifacts.
    std::vector<std::filesystem::path> dalvikMatches;
    if (std::filesystem::is_directory(dalvikRoot, ec)) {
        for (const auto& e : std::filesystem::directory_iterator(dalvikRoot, ec)) {
            const std::string name = e.path().filename().string();
            if (name == package_name || name.rfind(std::string(package_name) + "_", 0) == 0)
                dalvikMatches.push_back(e.path());
        }
    }
    uninstallDbg("dalvikMatches=" + std::to_string(dalvikMatches.size()));

    int success = 1;
    // Chia %: code 0-30, data 30-50, external data 50-68, obb 68-80,
    // media 80-88, dalvik 88-100.
    removeTreeWithProgress(appCodePath, "Removing app files",
                           cb, userdata, 0.0, 30.0, 0);
    removeTreeWithProgress(appDataPath, "Removing app data",
                           cb, userdata, 30.0, 20.0, 0);
    removeTreeWithProgress(extDataPath, "Removing external data",
                           cb, userdata, 50.0, 18.0, 0);
    removeTreeWithProgress(extObbPath, "Removing OBB files",
                           cb, userdata, 68.0, 12.0, 0);
    removeTreeWithProgress(extMediaPath, "Removing external media",
                           cb, userdata, 80.0, 8.0, 0);

    if (cb) cb("Removing compiled cache", 88, userdata);
    for (const auto& m : dalvikMatches) {
        removeTreeWithProgress(m, "Removing compiled cache",
                               cb, userdata, 88.0, 12.0, 0);
    }
    if (cb) cb("Done", 100, userdata);
    uninstallDbg("=== UNINSTALL DONE success=" + std::to_string(success) + " ===");
    if (success) ++s_installGeneration;
    return success;
}

extern "C" int kudroid_delete_app(const char* package_name) {
    return kudroid_delete_app_progress(package_name, nullptr, nullptr);
}

// Detailed debug log from the most recent uninstall operation.
// Returned string is malloced; caller must free().
extern "C" const char* kudroid_uninstall_debug_log(void) {
    char* out = static_cast<char*>(std::malloc(g_uninstallDbg.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, g_uninstallDbg.c_str(), g_uninstallDbg.size() + 1);
    return out;
}

extern "C" const char* kudroid_get_app_info(const char* package_name) {
    if (!package_name) return strdup("{}");

    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path appDir = std::filesystem::path(androidRoot) / "data/data" / package_name;

    uintmax_t totalSize = 0;
    std::error_code ec;

    if (std::filesystem::exists(appDir, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(appDir, ec)) {
            if (!std::filesystem::is_directory(entry.status(ec))) {
                totalSize += std::filesystem::file_size(entry, ec);
            }
        }
    }

    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"package_name\": \"%s\",\n"
        "  \"data_size_bytes\": %llu,\n"
        "  \"installed\": %s\n"
        "}",
        package_name,
        static_cast<unsigned long long>(totalSize),
        std::filesystem::exists(appDir, ec) ? "true" : "false");

    return strdup(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_syscall_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Syscall Traps (ARM64 .so) Test", path);
    if (!path) {
        log += "[kudroid_syscall] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_syscall] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_syscall] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_syscall] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_syscall] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_syscall] ELF mapped and Bionic imports bound.\n";
            mirrorCrash(log);
            void* address = loader.getSymbolAddress("kudroid_syscall_test");
            if (!address) {
                log += "[kudroid_syscall] SYMBOL FAILED: kudroid_syscall_test not found\n";
            } else {
                log += "[kudroid_syscall] Running kudroid_syscall_test()...\n";
                int (*test_func)() = reinterpret_cast<int (*)()>(address);

                // clear BionicShim trace buffer
                kudroid::bionic_shim_reset_trace();
                mirrorCrash(log);

                int result = test_func();
                log += "[kudroid_syscall] SYSCALL TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
            }
        }
        log += "[kudroid_syscall] Bionic shim trace:\n";
        log += "[BionicShim] ";
        log += kudroid::bionic_shim_trace();
        log += "\n";
    }
    appendCrashSnapshot(log, "log up to test end");
    writeLogFile("kudroid_syscall_test.txt", log);
    return strdup(log.c_str());
}

extern "C" const char* kudroid_jni_massive_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "Massive JNI 200+ Functions Test", path);
    if (!path) {
        log += "[kudroid_jni] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_jni] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_jni] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_jni] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_jni] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            loader.registerEhFrame();
            loader.executeInit();
            log += "[kudroid_jni] ELF mapped, relocated, and initialized.\n";
            mirrorCrash(log);

            // execute JNI_OnLoad if exported
            void* jniOnLoadAddr = loader.getSymbolAddress("JNI_OnLoad");
            if (jniOnLoadAddr) {
                log += "[kudroid_jni] Found JNI_OnLoad, executing...\n";
                mirrorCrash(log);
                // retrieve JavaVM from bridge
                using JNI_OnLoad_t = jint (*)(JavaVM*, void*);
                JNI_OnLoad_t jniOnLoad = reinterpret_cast<JNI_OnLoad_t>(jniOnLoadAddr);
                jint version = jniOnLoad(kuart_get_javavm(), nullptr);
                log += "[kudroid_jni] JNI_OnLoad returned version: 0x" + std::to_string(version) + "\n";
                // Same rule as the System.loadLibrary path: a library that left a
                // Java exception in flight must not have it attributed to whatever
                // runs next on this thread.
                const char* leaked = nullptr;
                if (kuart_take_pending_exception(&leaked)) {
                    log += "[kudroid_jni] JNI_OnLoad left a pending Java exception; cleared:\n";
                    log += leaked != nullptr ? leaked : "?";
                    log += "\n";
                }
                mirrorCrash(log);
            }

            void* address = loader.getSymbolAddress("kudroid_jni_massive_test");
            if (!address) {
                log += "[kudroid_jni] SYMBOL FAILED: kudroid_jni_massive_test not found\n";
            } else {
                log += "[kudroid_jni] Running kudroid_jni_massive_test()...\n";
                mirrorCrash(log);
                int (*test_func)(void*) = reinterpret_cast<int (*)(void*)>(address);

                // configure callbacks
                // Starts null: the pointer aims at this frame's string and must not be
                // left set after the function returns, or a later log callback writes
                // through a dead stack frame. It is cleared with the callback below.
                static std::string* g_jni_test_log = nullptr;
                g_jni_test_log = &log;
                kuart_set_log_callback([](const char* msg) {
                    if (g_jni_test_log != nullptr) {
                        *g_jni_test_log += "[KuART] ";
                        *g_jni_test_log += msg;
                        *g_jni_test_log += "\n";
                    }
                });

                kuart_init(""); // embedded framework is sufficient for this test
                mirrorCrash(log);
                void* vm = kuart_get_javavm();

                int result = test_func(vm);
                log += "[kudroid_jni] TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
                mirrorCrash(log);
                // Detach before leaving the frame the callback points into.
                kuart_set_log_callback(nullptr);
                g_jni_test_log = nullptr;
            }
        }
    }
    writeLogFile("kudroid_jni_massive_test.txt", log);
    return strdup(log.c_str());
}

// GPU .so execution test — loads ARM64 ELF test module via ELF loader,
// intercepting dlopen/dlsym calls to GPU libraries via BionicShim to map
// directly to iOS native graphics (MoltenVK / ANGLE).
// ─────────────────────────────────────────────────────────────────────────────

extern "C" const char* kudroid_gpu_vulkan_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "GPU Vulkan .so Intercept Test", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_gpu] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_gpu] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_gpu] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_gpu] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_gpu] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_gpu] ELF mapped and Bionic imports bound.\n";
            mirrorCrash(log);
            void* address = loader.getSymbolAddress("kudroid_gpu_vulkan_test");
            if (!address) {
                log += "[kudroid_gpu] SYMBOL FAILED: kudroid_gpu_vulkan_test not found\n";
            } else {
                log += "[kudroid_gpu] Running kudroid_gpu_vulkan_test()...\n";
                mirrorCrash(log);
                using VkTestFn = int (*)(uint32_t*);
                uint32_t ext_count = 0;
                const int result = reinterpret_cast<VkTestFn>(address)(&ext_count);
                log += "[kudroid_gpu] VULKAN TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)" : " (FAILED)") + "\n";
                log += "[kudroid_gpu] Vulkan extensions found: " + std::to_string(ext_count) + "\n";
            }
        }
    }

    appendCrashSnapshot(log, "log up to test end");

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_gpu] Bionic shim trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_gpu_vulkan_test.txt", log);
    return strdup(log.c_str());
}

extern "C" const char* kudroid_gpu_opengl_so_test(const char* path) {
    std::string log;
    appendTestHeader(log, "GPU OpenGL+EGL .so Intercept Test", path);
    kudroid::bionic_shim_reset_trace();
    if (!path) {
        log += "[kudroid_gpu] ERROR: null path\n";
    } else if (!kudroid_jit_available()) {
        log += "[kudroid_gpu] ABORT: JIT is Disabled — cannot execute ARM64 ELF\n";
    } else {
        kudroid::ElfLoader loader(path);
        if (!loader.parse()) {
            log += "[kudroid_gpu] PARSE FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.map()) {
            log += "[kudroid_gpu] MAP FAILED: " + std::string(loader.lastError()) + "\n";
        } else if (!loader.relocate()) {
            log += "[kudroid_gpu] RELOCATE FAILED: " + std::string(loader.lastError()) + "\n";
        } else {
            log += "[kudroid_gpu] ELF mapped and Bionic imports bound.\n";
            mirrorCrash(log);
            void* address = loader.getSymbolAddress("kudroid_gpu_opengl_test");
            if (!address) {
                log += "[kudroid_gpu] SYMBOL FAILED: kudroid_gpu_opengl_test not found\n";
            } else {
                log += "[kudroid_gpu] Running kudroid_gpu_opengl_test()...\n";
                mirrorCrash(log);
                using GlTestFn = int (*)();
                const int result = reinterpret_cast<GlTestFn>(address)();
                log += "[kudroid_gpu] OPENGL+EGL TEST RESULT: " +
                       std::to_string(result) + (result == 0 ? " (SUCCESS)" : " (FAILED)") + "\n";
            }
        }
    }

    appendCrashSnapshot(log, "log up to test end");

    const char* shimTrace = kudroid::bionic_shim_trace();
    if (shimTrace && *shimTrace) {
        log += "[kudroid_gpu] Bionic shim trace:\n";
        log += shimTrace;
    }

    writeLogFile("kudroid_gpu_opengl_test.txt", log);
    return strdup(log.c_str());
}

#include "kudroid/APKExtractor.h"

extern "C" const char* kudroid_load_apk(const char* apkPath) {
    // Use local buffer + strdup: avoids non-thread-safe static buffers.
    std::string log;
    appendTestHeader(log, "APK Loader execution", apkPath);

    if (!apkPath) {
        log += "[kudroid_apk] ERROR: APK path is null\n";
        return strdup(log.c_str());
    }

    std::string apkStr(apkPath);
    // Documents, not the log directory: an extracted APK is app data. These read
    // g_logDir back when the two were the same variable, which after the split would
    // have buried a whole extracted APK tree inside logs/.
    const char* docsRoot = g_docsDir[0] != '\0' ? g_docsDir : g_logDir;
    std::string targetDir = std::string(docsRoot) + "/extracted_apk";

    log += "[kudroid_apk] Initializing VFS...\n";
    kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(docsRoot);

    log += "[kudroid_apk] Extracting APK to: " + targetDir + "\n";
    bool extractedOk = false;
    if (kudroid::APKExtractor::is_bundle_container(apkStr)) {
        log += "[kudroid_apk] Split-APK bundle detected (.xapk/.apks/.apkm), merging splits...\n";
        extractedOk = kudroid::APKExtractor::extract_bundle(apkStr, targetDir);
    } else {
        extractedOk = kudroid::APKExtractor::extract_apk(apkStr, targetDir);
    }
    if (!extractedOk) {
        log += "[kudroid_apk] ERROR: Extraction failed - " + kudroid::APKExtractor::lastError() + "\n";
        return strdup(log.c_str());
    }
    log += "[kudroid_apk] Extracted successfully.\n";

    // Notify AAssetManager shim about extracted APK assets directory.
    kudroid_set_assets_dir((targetDir + "/assets").c_str());

    log += "[kudroid_apk] Scanning for native libraries (.so)...\n";
    // Process-lifetime (see globalLibraryManager) — JNI_OnLoad spawns guest render threads;
    // library mappings must persist after this function returns.
    kudroid::LibraryManager& libManager = globalLibraryManager();
    std::string libDir = targetDir + "/lib/" KUDROID_DEVICE_ABI;

    // ── KuART ──────────────────────────────────────────────────────────────
    // Load embedded framework.dex + APK classes*.dex.
    // No DEX-to-JAR conversion: KuART directly executes DEX bytecode.
    kuart_set_symbol_lookup([](const char* symbol) -> void* {
        return globalLibraryManager().resolveGlobalSymbol(symbol);
    });
    const bool kuartOk = kuart_init(targetDir.c_str()) != 0;
    if (kuartOk) {
        log += "[kudroid_apk] KuART ready: " + std::to_string(kuart_num_dex_files()) +
               " DEX loaded\n";
    } else {
        log += "[kudroid_apk] WARNING: KuART init failed (" + std::string(kuart_last_error()) + ")\n";
    }

    if (std::filesystem::exists(libDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                log += "[kudroid_apk] Loading library: " + entry.path().filename().string() + "\n";
                if (!libManager.loadRecursive(entry.path().string())) {
                    log += "[kudroid_apk] WARNING: Failed to load " + entry.path().filename().string() + "\n";
                } else {
                    log += "[kudroid_apk] Loaded successfully.\n";
                }
            }
        }
    } else {
        log += "[kudroid_apk] No native libraries found in " + libDir + "\n";
    }

    log += "[kudroid_apk] APK Load Complete.\n";
    return strdup(log.c_str());
}

// KuART DEX loading verification: parse and report contents.
// Returns malloc'ed log string; caller must free().
extern "C" const char* kudroid_translate_dex(const char* dexPath) {
    if (!dexPath || !*dexPath) {
        return strdup("[kudroid_dex] ERROR: null DEX path\n");
    }

    std::string log;
    log += "[kudroid_dex] Loading DEX with KuART: " + std::string(dexPath) + "\n";

    // Load DEX directory — KuART indexes all classes*.dex within the directory.
    const std::string dir = std::filesystem::path(dexPath).parent_path().string();
    if (!kuart_init(dir.c_str())) {
        log += "[kudroid_dex] LOAD FAILED: " + std::string(kuart_last_error()) + "\n";
        writeLogFile("kudroid_dex_translate.txt", log);
        return strdup(log.c_str());
    }

    log += "[kudroid_dex] DEX files loaded: " + std::to_string(kuart_num_dex_files()) + "\n";

    constexpr size_t kMaxList = 20000;
    std::vector<char*> classes(kMaxList, nullptr);
    const size_t n = kuart_list_app_classes(classes.data(), kMaxList);
    log += "[kudroid_dex] App classes found: " + std::to_string(n) + "\n";
    for (size_t i = 0; i < n && i < 20; ++i) {
        log += "[kudroid_dex]   " + std::string(classes[i]) + "\n";
    }
    if (n > 20) log += "[kudroid_dex]   ... (" + std::to_string(n - 20) + " more)\n";
    kuart_free_class_list(classes.data(), n);

    log += "[kudroid_dex] Classes resolved: " + std::to_string(kuart_num_loaded_classes()) + "\n";
    writeLogFile("kudroid_dex_translate.txt", log);
    return strdup(log.c_str());
}
