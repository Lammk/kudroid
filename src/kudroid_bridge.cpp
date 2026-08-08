#include "kudroid/elf_loader.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <string>
#include <fcntl.h>
#include <unistd.h>

// ── Persistent logging to the app's writable folder ─────────────────────────
// The app passes a directory (its Documents dir) via kudroid_set_log_dir().
// Success logs are written as .txt files; crashes (signal-based, so no C++
// exception fires) are captured by a signal handler that flushes the buffered
// log to disk using only async-signal-safe calls before re-raising.
static char g_logDir[1024] = {0};
static char g_crashBuf[16384];
static volatile sig_atomic_t g_crashLen = 0;

static void mirrorCrash(const std::string& log) {
    size_t n = log.size();
    if (n >= sizeof(g_crashBuf)) n = sizeof(g_crashBuf) - 1;
    memcpy(g_crashBuf, log.data(), n);
    g_crashBuf[n] = '\0';
    g_crashLen = (sig_atomic_t)n;
}

static void writeLogFile(const char* name, const std::string& content) {
    if (!g_logDir[0]) return;
    std::string path = std::string(g_logDir) + "/" + name;
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        fwrite(content.data(), 1, content.size(), f);
        fclose(f);
    }
}

static void crashHandler(int sig) {
    if (g_logDir[0]) {
        // Build "<dir>/kudroid_crash.log" without heap allocation.
        char path[1200];
        size_t dl = strlen(g_logDir);
        if (dl > sizeof(path) - 32) dl = sizeof(path) - 32;
        memcpy(path, g_logDir, dl);
        const char* suffix = "/kudroid_crash.log";
        memcpy(path + dl, suffix, strlen(suffix) + 1);

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const char* hdr = "[kudroid_core] CRASH — fatal signal caught\n";
            (void)!write(fd, hdr, strlen(hdr));
            char sigline[64];
            int m = snprintf(sigline, sizeof(sigline), "signal = %d\n", sig);
            if (m > 0) (void)!write(fd, sigline, (size_t)m);
            (void)!write(fd, "--- log up to crash ---\n", 24);
            (void)!write(fd, g_crashBuf, (size_t)g_crashLen);
            close(fd);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void installCrashHandlers(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;
    signal(SIGILL,  crashHandler);
    signal(SIGBUS,  crashHandler);
    signal(SIGSEGV, crashHandler);
    signal(SIGTRAP, crashHandler);
    signal(SIGABRT, crashHandler);
}

extern "C" void kudroid_set_log_dir(const char* dir) {
    if (!dir) return;
    strncpy(g_logDir, dir, sizeof(g_logDir) - 1);
    g_logDir[sizeof(g_logDir) - 1] = '\0';
    installCrashHandlers();
}

#if defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#include <TargetConditionals.h>

// csops() is a private API but stable; used to read the process' code-signing
// status. CS_DEBUGGED is set when the JIT (dynamic-codesigning) path is active
// under LiveContainer / a debugger, which is what lets PROT_EXEC pages run.
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif

// Returns 1 if JIT (executable memory) appears usable, 0 otherwise.
static int kudroid_jit_available(void) {
    unsigned int flags = 0;
    // CS_DEBUGGED is the only reliable signal on iOS: it is set when a debugger
    // (LiveContainer/debugserver) has enabled dynamic code signing, which is
    // exactly what permits executing PROT_EXEC pages. An RWX mmap probe is NOT
    // reliable — the syscall succeeds without JIT, but execution still faults,
    // giving a false "Enabled".
    if (csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) != 0) {
        return 0;
    }
    return (flags & CS_DEBUGGED) ? 1 : 0;
}
#else
static int kudroid_jit_available(void) { return 1; }
#endif

extern "C" const char* kudroid_jit_status(void) {
    const char* text = kudroid_jit_available()
        ? "JIT: Enabled"
        : "JIT: Disabled";
    char* result = (char*)malloc(strlen(text) + 1);
    if (result) memcpy(result, text, strlen(text) + 1);
    return result;
}

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
    log += "[kudroid_core] Self-test starting...\n";
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

        log += "[kudroid_core] Calling parse()...\n";
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

        log += "[kudroid_core] Parsing ELF headers...\n";
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

            // Try map (stub for now)
            log += "[kudroid_core] Mapping segments...\n";
            if (loader.map()) {
                log += "[kudroid_core] Map OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Map failed: %s\n", loader.lastError());
                log += buf;
            }

            // Try relocate (stub for now)
            log += "[kudroid_core] Relocating...\n";
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
    if (!path) {
        log += "[kudroid_core] ERROR: null path\n";
        char* result = (char*)malloc(log.size() + 1);
        if (result) memcpy(result, log.c_str(), log.size() + 1);
        return result;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "[kudroid_core] Execution test for: %s\n", path);
    log += buf;

    // Refuse to run native code when JIT is off: executing PROT_EXEC pages
    // without dynamic code signing faults the whole process, and the buffered
    // log below would never be returned. Fail loudly instead of crashing.
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

        log += "[kudroid_core] ELF parsed OK.\n";

        if (!loader.map()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] MAP FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] mmap OK.\n";

        if (!loader.relocate()) {
            snprintf(buf, sizeof(buf), "[kudroid_core] RELOCATE FAILED: %s\n", loader.lastError());
            log += buf;
            writeLogFile("kudroid_exec.txt", log);
            char* result = (char*)malloc(log.size() + 1);
            if (result) memcpy(result, log.c_str(), log.size() + 1);
            return result;
        }

        log += "[kudroid_core] Relocate OK.\n";
        log += "[kudroid_core] Running testExecution()...\n";

        // Snapshot the log for the crash handler: the call below jumps into
        // JIT'd code and may fault (signal, not exception). If it does, the
        // handler flushes this buffer to kudroid_crash.log.
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