#include "kudroid/kudroid_bridge.h"
#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/APKExtractor.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/AssetShim.h"
#include "kudroid/PermissionManager.h"
#include "kudroid/KuArtRuntime.h"
#include "kudroid/platform/JavaCanvasRenderer.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <csignal>
#include <ctime>
#include <cctype>
#include <cerrno>
#include <setjmp.h>
#include <filesystem>
#include <set>
#include <string>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ucontext.h>
#include <dlfcn.h>
#include <unwind.h>
#include <mutex>
#include <atomic>

extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

// ── ghi nhật ký liên tục vào thư mục có thể ghi của ứng dụng ─────────────────────────
// ứng dụng truyền một thư mục (thư mục documents của nó) thông qua kudroid_set_log_dir().
// nhật ký thành công được ghi dưới dạng tệp .txt; các sự cố (dựa trên tín hiệu, do đó không có ngoại lệ
// c++ nào được kích hoạt) được trình xử lý tín hiệu bắt, bộ đệm nhật ký được đẩy
// vào đĩa chỉ bằng các lệnh gọi an toàn tín hiệu bất đồng bộ trước khi kích hoạt lại.
//
// Two directories, deliberately distinct:
//
//   g_docsDir — the app's Documents. Where installed apps, extracted APKs and the
//               VFS root live; the host hands this in.
//   g_logDir  — g_docsDir + "/logs". Every diagnostic file goes here.
//
// They used to be the same, so eight log files sat among put_apk_here/, android_root/,
// micro_tests/ and extracted_apk/ at the top level of Documents. Nothing about that
// was wrong, only unreadable — and it made "pull the logs" a matter of knowing which
// eight of a dozen entries were logs.
static char g_docsDir[1024] = {0};
static char g_logDir[1024] = {0};
const char* g_kudroid_log_dir_ptr = g_logDir;

// 16KB trước đây quá nhỏ: đống dòng ELF-loading/`[kudroid_core]` lấp đầy buffer
// và đẩy phần quan trọng (eglGetDisplay/eglInitialize ngay trước crash) ra
// ngoài — log bị cắt đúng chỗ quan trọng. 256KB, static nên an toàn trong
// signal handler (không cấp phát heap).
static char g_crashBuf[262144];
static volatile sig_atomic_t g_crashLen = 0;
static std::mutex g_crashBufMtx;
static char g_abortMessage[1024] = {0};

// ── Lá chắn abort() cho JNI_OnLoad ───────────────────────────────────────────
// Một số thư viện phụ của game (conscrypt, HttpClient, maesdk…) gọi abort()
// ngay trong JNI_OnLoad khi không tìm được method/field chúng cần. abort() là
// SIGABRT nên try/catch KHÔNG bắt được → cả process chết. Guard dưới đây cho
// phép nhảy ngược ra khỏi JNI_OnLoad khi nó abort, bỏ qua thư viện đó và tiếp
// tục khởi động app. Chỉ bật quanh đúng lời gọi JNI_OnLoad, không bật lúc khác
// (crash thật vẫn phải ghi log đầy đủ).
static thread_local sigjmp_buf g_jniGuardJmp;
static thread_local volatile sig_atomic_t g_jniGuardActive = 0;
static thread_local int g_jniGuardSignal = 0;

// Machine state captured when the guard swallows a signal.
//
// The guard used to siglongjmp straight out of the signal handler, which is before
// crashHandler writes kudroid_crash.log — so a library that segfaulted inside
// JNI_OnLoad produced one WARNING line and nothing else: no pc, no fault address,
// no stack. There was no way to tell what it touched.
//
// The handler cannot format or write safely, so it only copies scalars here (all
// async-signal-safe stores) and the caller reports them after the jump returns.
struct JniGuardFault {
    int signal = 0;
    int si_code = 0;
    const void* fault_addr = nullptr;
    uint64_t pc = 0;
    uint64_t lr = 0;
    uint64_t sp = 0;
    uint64_t fp = 0;
    uint64_t x[9] = {0};
    // Stack words from sp, copied in the handler because by the time the caller
    // runs the frame is gone.
    uint64_t stack[32] = {0};
    bool have_regs = false;
};
static thread_local JniGuardFault g_jniGuardFault;

// Stack riêng cho signal handler — để ở file scope vì sau khi siglongjmp ra
// khỏi handler, kernel/libc vẫn coi alt stack đang "onstack"; phải arm lại nếu
// không lần crash sau sẽ không chạy được trên stack riêng.
static char g_altSignalStack[64 * 1024];
static bool g_altStackArmed = false;

static void armAltSignalStack(void) {
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_altSignalStack;
    ss.ss_size = sizeof(g_altSignalStack);
    ss.ss_flags = 0;
    g_altStackArmed = (sigaltstack(&ss, nullptr) == 0);
}

// Gọi JNI_OnLoad với lá chắn signal. Đặt sigsetjmp trong một hàm riêng để
// longjmp không thể clobber biến local của hàm gọi (-Wclobbered).
// Trả về: 0 = chạy xong bình thường (*outVersion hợp lệ),
// -1 = C++ exception, >0 = số signal đã bị chặn.
//
// A guarded library often calls back into Java before it faults (RegisterNatives,
// GetMethodID, an actual Java call), so the interpreter may have live frames when
// the jump happens. siglongjmp does not unwind, so those frames' scope guards never
// run and the interpreter is left describing frames that no longer exist — the next
// exception on this thread then crashes rendering its own stack trace. Snapshot the
// interpreter's per-thread bookkeeping before the call and put it back after any
// abnormal return.
__attribute__((noinline))
static int kudroid_call_jni_onload_guarded(jint (*fn)(JavaVM*, void*),
                                          JavaVM* vm, jint* outVersion) {
    // volatile: read after siglongjmp, so it must not live only in a register the
    // jump restores to its pre-call value.
    static thread_local volatile size_t kuartState[KUART_THREAD_STATE_WORDS] = {0};
    kuart_save_thread_state(const_cast<size_t*>(kuartState));

    if (sigsetjmp(g_jniGuardJmp, 1) != 0) {
        // Quay lại từ signal handler — arm lại alt stack vì siglongjmp không
        // rời alt stack một cách bình thường.
        if (g_altStackArmed) armAltSignalStack();
        kuart_restore_thread_state(const_cast<const size_t*>(kuartState));
        return g_jniGuardSignal > 0 ? g_jniGuardSignal : 1;
    }
    g_jniGuardActive = 1;
    g_jniGuardFault = JniGuardFault{};
    int rc = 0;
    try {
        *outVersion = fn(vm, nullptr);
    } catch (...) {
        rc = -1;
    }
    g_jniGuardActive = 0;
    // A C++ exception thrown through the guest library unwinds C++ frames but the
    // interpreter's guards are only on Execute() frames it owns; a library that
    // throws across a JNI boundary can still skip them.
    if (rc != 0) kuart_restore_thread_state(const_cast<const size_t*>(kuartState));
    return rc;
}

static int kudroid_jit_available(void);
// Build stamp — định nghĩa phía dưới file, dùng trong appendTestHeader ở trên nó.
extern "C" const char* kudroid_build_stamp(void);

// Gentle crash variables:
static std::atomic<bool> g_hasCrashed{false};
static char g_lastCrashTail[16384] = {0};
static pthread_t g_mainThread = 0;

static void extractLastLines(const char* src, size_t srcLen, char* dst, size_t dstCap, int maxLines = 30) {
    if (!src || srcLen == 0 || dstCap == 0) {
        if (dstCap > 0) dst[0] = '\0';
        return;
    }
    int lineCount = 0;
    const char* p = src + srcLen - 1;
    while (p >= src && (*p == '\n' || *p == '\r')) p--;
    const char* end = p + 1;
    while (p >= src) {
        if (*p == '\n') {
            lineCount++;
            if (lineCount >= maxLines) {
                p++;
                break;
            }
        }
        p--;
    }
    if (p < src) p = src;
    size_t len = static_cast<size_t>(end - p);
    if (len >= dstCap) len = dstCap - 1;
    memcpy(dst, p, len);
    dst[len] = '\0';
}

// Game logs (bionic_android_log_print) mirror vào đây để file crash chứa log
// của game ngay trước khi crash. Ring buffer — giữ phần mới nhất.
extern "C" void kudroid_append_crash_log(const char* text, size_t len) {
    if (!text || len == 0) return;
    if (len > 8192) len = 8192; // giới hạn một dòng
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    size_t cur = static_cast<size_t>(g_crashLen);
    const size_t cap = sizeof(g_crashBuf) - 1;
    if (len > cap - cur) {
        const size_t drop = len - (cap - cur);
        if (drop < cur) {
            memmove(g_crashBuf, g_crashBuf + drop, cur - drop);
            cur -= drop;
        } else {
            cur = 0;
        }
    }
    memcpy(g_crashBuf + cur, text, len);
    cur += len;
    g_crashBuf[cur] = '\0';
    g_crashLen = static_cast<sig_atomic_t>(cur);
}

// android_set_abort_message (từ game): lưu lại để crash handler in ra.
extern "C" void kudroid_store_abort_message(const char* msg) {
    if (!msg) return;
    strncpy(g_abortMessage, msg, sizeof(g_abortMessage) - 1);
    g_abortMessage[sizeof(g_abortMessage) - 1] = '\0';
}

// ── Bắt abort message phía HOST (không phải từ game) ─────────────────────────
// SIGABRT thường là abort() sau một exception/assert không bắt được. Trước đây
// chỉ có android_set_abort_message (từ guest) mới lưu được message → crash log
// thiếu lý do. Hai handler dưới đây bắt lý do trước khi abort:
// 1. ObjC exception chưa bắt (NSException — ANGLE/Metal/UIKit hay ném) → reason
// 2. C++ exception chưa bắt (std::terminate) → what()
#if defined(__APPLE__)
#include <exception>
extern "C" {
extern void* objc_msgSend(void* self, void* op, ...);
extern void* sel_registerName(const char* name);
extern void NSSetUncaughtExceptionHandler(void (*handler)(void* exception));
}

static void kudroid_uncaught_objc_handler(void* exception) {
    // exception là NSException* — [exception reason] → NSString* → UTF8String.
    if (!exception) return;
    void* reason = objc_msgSend(exception, sel_registerName("reason"));
    if (reason) {
        const char* utf8 = static_cast<const char*>(
            objc_msgSend(reason, sel_registerName("UTF8String")));
        if (utf8) kudroid_store_abort_message(utf8);
    }
}

static void kudroid_terminate_handler() {
    if (std::current_exception()) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& e) {
            if (e.what() && e.what()[0]) kudroid_store_abort_message(e.what());
        } catch (...) {
            kudroid_store_abort_message("uncaught non-std C++ exception");
        }
    }
    std::abort();
}
#endif

// snprintf trả về độ dài "sẽ ghi" (có thể LỚN HƠN buffer khi bị cắt) — ghi đủ
// m byte từ buffer nhỏ là đọc tràn stack → đống rác nhị phân trong crash log
// (chính là vụ pc_sym của Discord bị cắt cụt + garbage). Clamp trước khi write.
static void crashWriteLine(int fd, const char* buf, int len, size_t bufSize) {
    if (len <= 0 || !buf) return;
    size_t n = (size_t)len;
    if (n >= bufSize) n = bufSize - 1;
    (void)!write(fd, buf, n);
}

// Unwind bằng _Unwind_Backtrace (không dùng heap) + dladdr (best effort).
struct UnwindContext {
    int fd;
    int count;
};

static _Unwind_Reason_Code unwindCallback(struct _Unwind_Context* ctx, void* arg) {
    UnwindContext* u = static_cast<UnwindContext*>(arg);
    if (u->count >= 20) return _URC_END_OF_STACK;
    const uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc != 0) {
        char line[512];
        Dl_info info;
        int m;
        if (dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname) {
            if (info.dli_sname) {
                const long offset = (long)(pc - (uintptr_t)info.dli_saddr);
                m = snprintf(line, sizeof(line), "  #%02d pc 0x%llx  %s+0x%lx (%s)\n",
                             u->count, (unsigned long long)pc, info.dli_sname,
                             offset, info.dli_fname);
            } else {
                const long offset = (long)(pc - (uintptr_t)info.dli_fbase);
                m = snprintf(line, sizeof(line),
                             "  #%02d pc 0x%llx  +0x%lx (%s)\n",
                             u->count, (unsigned long long)pc, offset,
                             info.dli_fname);
            }
        } else {
            char guest[512];
            if (kudroid::kudroid_lookup_guest_module(
                    reinterpret_cast<void*>(pc), guest, sizeof(guest))) {
                m = snprintf(line, sizeof(line), "  #%02d pc 0x%llx  %s\n",
                             u->count, (unsigned long long)pc, guest);
            } else {
                m = snprintf(line, sizeof(line), "  #%02d pc 0x%llx\n",
                             u->count, (unsigned long long)pc);
            }
        }
        crashWriteLine(u->fd, line, m, sizeof(line));
    }
    u->count++;
    return _URC_NO_REASON;
}

static void writeBacktrace(int fd) {
    UnwindContext ctx = {fd, 0};
    _Unwind_Backtrace(unwindCallback, &ctx);
}

static void mirrorCrash(const std::string& log) {
    // Cùng mutex với kudroid_append_crash_log — ghi đè không lock là data race
    // với luồng game đang log (có thể làm hỏng g_crashLen/g_crashBuf).
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
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

// Snapshot nội dung crash buffer (log gần nhất từ game/shim) — có lock để đọc
// an toàn với luồng game đang log. Dùng để file test chứa log chi tiết của
// shim (trước đây chỉ nằm trong crash buffer, mất khi test thành công).
extern "C" const char* kudroid_crash_log_snapshot(void) {
    std::lock_guard<std::mutex> lock(g_crashBufMtx);
    const size_t n = static_cast<size_t>(g_crashLen);
    char* out = static_cast<char*>(std::malloc(n + 1));
    if (!out) return nullptr;
    std::memcpy(out, g_crashBuf, n);
    out[n] = '\0';
    return out;
}

// Append snapshot crash buffer vào cuối log test (phần "log up to test").
static void appendCrashSnapshot(std::string& log, const char* sectionName) {
    const char* snap = kudroid_crash_log_snapshot();
    if (!snap) return;
    if (*snap) {
        log += std::string("\n--- ") + sectionName + " ---\n";
        log += snap;
    }
    std::free(const_cast<char*>(snap));
}

static void appendTestHeader(std::string& log, const char* test, const char* path) {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    // Build stamp (version + commit hash) ở ĐẦU mỗi log — để phân biệt bản IPA
    // đang chạy với bản cũ ngay từ dòng đầu tiên của bất kỳ log nào.
    log += "[kudroid_core] Build: " + std::string(kudroid_build_stamp()) + "\n";
    log += "[kudroid_core] ===== " + std::string(test) + " =====\n";
    log += "[kudroid_core] Timestamp: " + oss.str() + "\n";
    if (path && std::strcmp(path, "N/A") != 0) {
        log += "[kudroid_core] Path: " + std::string(path) + "\n";
    }
#if defined(__APPLE__)
    log += "[kudroid_core] JIT: " +
           std::string(kudroid_jit_available() ? "Enabled" : "Disabled") + "\n";
#endif
}

// Nhãn build: để phân biệt bản IPA đang chạy có phải bản mới nhất hay không.
// Khi user nghi ngờ "iPhone vẫn chạy app cũ" — so stamp này trong
// kudroid_crash.log với stamp của bản build mới nhất trên CI.
extern "C" const char* kudroid_build_stamp(void) {
    static const char kStamp[] =
        "kudroid_core v0.6.5 " __DATE__ " " __TIME__ " "
#ifdef KUDROID_GIT_HASH
        KUDROID_GIT_HASH
#else
        "(no-git-hash)"
#endif
        ;
    return kStamp;
}

#if defined(__aarch64__) || defined(__arm64__)
// Symbolicate một địa chỉ thành "tên hàm+offset (module)" hoặc "module+offset" —
// để nhìn pc là biết chính xác chết trong hàm nào, không còn (no symbol) mù mờ.
// Ưu tiên registry guest (region do ELF loader mmap — dladdr không biết), sau đó
// dladdr cho symbol host, cuối cùng in raw + module base offset.
static void symbolicateAddr(uintptr_t pc, char* out, size_t outSize) {
    if (kudroid::kudroid_lookup_guest_module(reinterpret_cast<void*>(pc), out, outSize)) {
        return;
    }
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(pc), &info) != 0 && info.dli_fname) {
        if (info.dli_sname) {
            const long offset = (long)(pc - (uintptr_t)info.dli_saddr);
            snprintf(out, outSize, "%s+0x%lx (%s)", info.dli_sname, offset,
                     info.dli_fname);
        } else {
            const long offset = (long)(pc - (uintptr_t)info.dli_fbase);
            snprintf(out, outSize, "0x%llx+0x%lx (%s)",
                     (unsigned long long)pc, offset, info.dli_fname);
        }
    } else {
        snprintf(out, outSize, "0x%llx (no symbol)", (unsigned long long)pc);
    }
}
#endif

// Render the state captured when the JNI_OnLoad guard swallowed a signal.
//
// The guard jumps out of the signal handler before crashHandler's reporting code
// runs, so a library that faulted inside JNI_OnLoad produced a single WARNING line
// and nothing usable. This produces the same information the crash log carries —
// signal, fault address, pc/lr symbolicated, registers, raw stack — for a fault the
// process survived.
static std::string describeJniGuardFault(const std::string& library, int guardRc) {
    const JniGuardFault& f = g_jniGuardFault;
    std::string out;
    char line[1024];

    snprintf(line, sizeof(line),
             "[kudroid_core] === JNI_OnLoad fault in %s ===\n", library.c_str());
    out += line;
    snprintf(line, sizeof(line), "build: %s\n", kudroid_build_stamp());
    out += line;

    // guardRc > 0 is the signal number; -1 means a C++ exception escaped, which
    // never reaches the handler and therefore has no register state.
    if (guardRc < 0) {
        out += "cause: C++ exception escaped JNI_OnLoad (no signal, no registers)\n";
        return out;
    }

    const int sig = f.signal != 0 ? f.signal : guardRc;
    const char* signame = "?";
    switch (sig) {
        case SIGSEGV: signame = "SIGSEGV"; break;
        case SIGBUS:  signame = "SIGBUS";  break;
        case SIGABRT: signame = "SIGABRT"; break;
        case SIGILL:  signame = "SIGILL";  break;
        case SIGTRAP: signame = "SIGTRAP"; break;
        default: break;
    }
    snprintf(line, sizeof(line), "signal = %d (%s)\nsi_code = %d\nfault_addr = %p\n",
             sig, signame, f.si_code, f.fault_addr);
    out += line;

    // A near-null fault address is the signature of dereferencing a JNI handle that
    // came back null — usually a FindClass/GetMethodID that failed and whose return
    // value the library never checked.
    const uintptr_t addr = reinterpret_cast<uintptr_t>(f.fault_addr);
    if (sig == SIGSEGV && addr < 0x10000) {
        snprintf(line, sizeof(line),
                 "note: fault address is near null (offset 0x%llx) — typically a null "
                 "JNI handle dereferenced without checking the return value\n",
                 (unsigned long long)addr);
        out += line;
    }

    if (!f.have_regs) {
        out += "registers: unavailable on this platform\n";
        return out;
    }

    snprintf(line, sizeof(line), "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\nfp = 0x%llx\n",
             (unsigned long long)f.pc, (unsigned long long)f.lr,
             (unsigned long long)f.sp, (unsigned long long)f.fp);
    out += line;

#if defined(__aarch64__) || defined(__arm64__)
    {
        char symPc[512];
        char symLr[512];
        symbolicateAddr(static_cast<uintptr_t>(f.pc), symPc, sizeof(symPc));
        symbolicateAddr(static_cast<uintptr_t>(f.lr), symLr, sizeof(symLr));
        snprintf(line, sizeof(line), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
        out += line;
    }
#endif

    for (int i = 0; i < 9; ++i) {
        snprintf(line, sizeof(line), "x%d = 0x%llx\n", i, (unsigned long long)f.x[i]);
        out += line;
    }

    out += "--- stack from sp ---\n";
    for (int i = 0; i < 32; i += 4) {
        snprintf(line, sizeof(line), "sp%+04d: %016llx  %016llx  %016llx  %016llx\n",
                 i * 8, (unsigned long long)f.stack[i],
                 (unsigned long long)f.stack[i + 1],
                 (unsigned long long)f.stack[i + 2],
                 (unsigned long long)f.stack[i + 3]);
        out += line;
    }

#if defined(__aarch64__) || defined(__arm64__)
    // Walk the frame chain so the caller inside the library is named, not just the
    // faulting instruction. Range-checked at every step: a bad fp must not turn a
    // survivable fault into a real crash.
    out += "--- fp chain ---\n";
    {
        uint64_t fp = f.fp;
        for (int depth = 0; depth < 24; ++depth) {
            if (!(fp > 0x1000 && fp < 0x7fffffffffffULL)) break;
            const uint64_t* p = reinterpret_cast<const uint64_t*>(fp);
            const uint64_t savedFp = p[0];
            const uint64_t savedLr = p[1];
            if (savedLr == 0) break;
            char sym[512];
            symbolicateAddr(static_cast<uintptr_t>(savedLr), sym, sizeof(sym));
            snprintf(line, sizeof(line), "  #%02d lr=0x%llx  %s\n", depth,
                     (unsigned long long)savedLr, sym);
            out += line;
            if (!(savedFp > fp && savedFp < 0x7fffffffffffULL)) break;
            fp = savedFp;
        }
    }
#endif

    return out;
}

// Append to kudroid_crash.log rather than overwrite: several libraries can fault in
// one launch, and crashHandler truncates the file for a real crash. Losing the
// earlier faults would hide the first one, which is usually the informative one.
static void appendCrashLogFile(const std::string& content) {
    if (!g_logDir[0]) return;
    const std::string path = std::string(g_logDir) + "/kudroid_crash.log";
    FILE* fp = fopen(path.c_str(), "a");
    if (fp == nullptr) return;
    fwrite(content.data(), 1, content.size(), fp);
    fclose(fp);
}

static void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGTRAP) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            return; // đã xử lý thành công, tiếp tục thực thi!
        }
    }

    // Đang ở trong một lời gọi JNI_OnLoad được bọc guard: thư viện đó abort/segfault
    // thì bỏ qua nó thay vì giết process. Chỉ dùng hàm async-signal-safe ở đây;
    // việc ghi log để phía gọi làm sau khi siglongjmp trả về.
    //
    // Capture the machine state first. This path never reaches the crash-log code
    // below, so without this a swallowed fault left no pc, no fault address and no
    // stack — only a WARNING line saying a signal happened. Copying scalars is
    // async-signal-safe; formatting and writing are left to the caller.
    if (g_jniGuardActive && (sig == SIGABRT || sig == SIGSEGV || sig == SIGBUS ||
                             sig == SIGILL  || sig == SIGTRAP)) {
        JniGuardFault& f = g_jniGuardFault;
        f.signal = sig;
        if (info != nullptr) {
            f.si_code = info->si_code;
            f.fault_addr = info->si_addr;
        }
#if (defined(__aarch64__) || defined(__arm64__)) && defined(__APPLE__)
        if (ucontext != nullptr) {
            ucontext_t* uc = static_cast<ucontext_t*>(ucontext);
            f.pc = uc->uc_mcontext->__ss.__pc;
            f.lr = uc->uc_mcontext->__ss.__lr;
            f.sp = uc->uc_mcontext->__ss.__sp;
            f.fp = uc->uc_mcontext->__ss.__fp;
            for (int i = 0; i < 9; ++i) f.x[i] = uc->uc_mcontext->__ss.__x[i];
            // Only read the stack when sp looks like a mapped address: a
            // double fault inside the handler would take down the process for
            // real, which is exactly what the guard exists to avoid.
            if (f.sp > 0x1000 && f.sp < 0x7fffffffffffULL) {
                const uint64_t* stack = reinterpret_cast<const uint64_t*>(f.sp);
                for (int i = 0; i < 32; ++i) f.stack[i] = stack[i];
            }
            f.have_regs = true;
        }
#else
        (void)ucontext;
#endif
        g_jniGuardActive = 0;
        g_jniGuardSignal = sig;
        siglongjmp(g_jniGuardJmp, 1);
    }

    // Xả bộ đệm stdout/stderr — các log [KuDroidGPU]/[AndroidLog] đang nằm trong
    // buffer (block-buffered khi redirect) sẽ mất nếu không flush trước khi chết.
    // (không async-signal-safe hoàn hảo nhưng là crash handler chẩn đoán, chấp nhận được)
    fflush(stdout);
    fflush(stderr);
    
    if (g_logDir[0]) {
        // xây dựng "<dir>/kudroid_crash.log" mà không cấp phát vùng nhớ heap.
        char path[1200];
        size_t dl = strlen(g_logDir);
        if (dl >= sizeof(path) - 32) dl = sizeof(path) - 32;
        memcpy(path, g_logDir, dl);
        const char* suffix = "/kudroid_crash.log";
        memcpy(path + dl, suffix, strlen(suffix) + 1);

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            const char* hdr = "[kudroid_core] CRASH — fatal signal caught\n";
            (void)!write(fd, hdr, strlen(hdr));
            const char* stamp = kudroid_build_stamp();
            char stampLine[512];
            int sm = snprintf(stampLine, sizeof(stampLine), "build: %s\n", stamp);
            crashWriteLine(fd, stampLine, sm, sizeof(stampLine));
            char sigline[2048];
            int m = snprintf(sigline, sizeof(sigline), "signal = %d\n", sig);
            crashWriteLine(fd, sigline, m, sizeof(sigline));

            // Ghi tên thread đang crash (game chạy nhiều thread — biết thread nào
            // chết là một nửa chẩn đoán). pthread_getname_np không hoàn toàn
            // async-signal-safe nhưng là crash handler chẩn đoán, chấp nhận được.
            {
                char tname[64] = "?";
#if defined(__APPLE__)
                pthread_getname_np(pthread_self(), tname, sizeof(tname));
#else
                (void)pthread_getname_np(pthread_self(), tname, sizeof(tname));
#endif
                m = snprintf(sigline, sizeof(sigline), "thread = %s (0x%llx)\n",
                             tname[0] ? tname : "?",
                             (unsigned long long)(uintptr_t)pthread_self());
                crashWriteLine(fd, sigline, m, sizeof(sigline));
            }

            // in địa chỉ lỗi
            if (info) {
                m = snprintf(sigline, sizeof(sigline),
                    "fault_addr = %p\nsi_code = %d\n",
                    info->si_addr, info->si_code);
                crashWriteLine(fd, sigline, m, sizeof(sigline));
            }

            // in thanh ghi pc (arm64)
#if defined(__aarch64__) || defined(__arm64__)
            if (ucontext) {
#if defined(__APPLE__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext->__ss.__pc;
                uint64_t lr = uc->uc_mcontext->__ss.__lr;
                uint64_t sp = uc->uc_mcontext->__ss.__sp;
                uint64_t fp = uc->uc_mcontext->__ss.__fp;
                uint64_t cpsr = uc->uc_mcontext->__ss.__cpsr;
                uint64_t x0 = uc->uc_mcontext->__ss.__x[0];
                uint64_t x1 = uc->uc_mcontext->__ss.__x[1];
                uint64_t x2 = uc->uc_mcontext->__ss.__x[2];
                uint64_t x3 = uc->uc_mcontext->__ss.__x[3];
                uint64_t x4 = uc->uc_mcontext->__ss.__x[4];
                uint64_t x5 = uc->uc_mcontext->__ss.__x[5];
                uint64_t x6 = uc->uc_mcontext->__ss.__x[6];
                uint64_t x7 = uc->uc_mcontext->__ss.__x[7];
                uint64_t x8 = uc->uc_mcontext->__ss.__x[8];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\nsp = 0x%llx\nfp = 0x%llx\ncpsr = 0x%llx\n"
                    "x0 = 0x%llx\nx1 = 0x%llx\nx2 = 0x%llx\nx3 = 0x%llx\n"
                    "x4 = 0x%llx\nx5 = 0x%llx\nx6 = 0x%llx\nx7 = 0x%llx\nx8 = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr, (unsigned long long)sp,
                    (unsigned long long)fp, (unsigned long long)cpsr,
                    (unsigned long long)x0, (unsigned long long)x1, (unsigned long long)x2,
                    (unsigned long long)x3, (unsigned long long)x4, (unsigned long long)x5,
                    (unsigned long long)x6, (unsigned long long)x7, (unsigned long long)x8);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // Symbolicate pc/lr để log có ý nghĩa (file + hàm gần nhất).
                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                // Raw stack dump từ faulting sp. Tại thời điểm abort, sp đang ở
                // trong abort()/pthread_kill; frame phía trên (vùng nhớ cao hơn)
                // chứa frame của caller — vd __cxa_guard_acquire (prologue
                // `stp x29,x30,[sp,#-64]!` + `stp x20,x19,[sp,#48]`) lưu guard
                // pointer tại [fp+56] và caller LR tại [fp+8]. Dump thô để có
                // thể decode guard + caller khi debug crash.
                (void)!write(fd, "\n--- stack from sp ---\n", 22);
                {
                    const uint64_t* stack = reinterpret_cast<const uint64_t*>(sp);
                    for (int i = 0; i < 128; i += 4) {
                        int n = snprintf(sigline, sizeof(sigline),
                            "sp%+04d: %016llx  %016llx  %016llx  %016llx\n",
                            i * 8,
                            (unsigned long long)stack[i],
                            (unsigned long long)stack[i + 1],
                            (unsigned long long)stack[i + 2],
                            (unsigned long long)stack[i + 3]);
                        crashWriteLine(fd, sigline, n, sizeof(sigline));
                    }
                }

                // Walk frame chain từ faulting fp (ucontext) — tìm frame của
                // __cxa_guard_acquire (layout cố định `stp x20,x19,[sp,#48]`):
                // [fp+8] = saved LR (caller), [fp+56] = x19 = guard pointer.
                // Chỉ đọc các frame đã qua kiểm tra dải (savedFp > f, delta hữu
                // hạn) để không double-fault trong signal handler. Không deref
                // slot56 (guard) — chỉ dump raw, decode offline.
                (void)!write(fd, "\n--- fp chain ---\n", 17);
                {
                    uint64_t f = fp;
                    for (int i = 0; i < 32; ++i) {
                        if (!(f > 0x1000 && f < 0x7fffffffffffULL)) break;
                        const uint64_t* p = reinterpret_cast<const uint64_t*>(f);
                        const uint64_t savedFp = p[0];
                        const uint64_t savedLr = p[1];
                        const uint64_t slot56  = p[7]; // [fp+56]
                        char lrMod[256] = {0};
                        const bool inGuest =
                            kudroid::kudroid_lookup_guest_module(
                                reinterpret_cast<void*>(savedLr), lrMod, sizeof(lrMod));
                        // Nếu không thuộc guest ELF thì đây là code HOST — gồm cả
                        // Avian (link tĩnh vào KuDroidShell). Trước đây chỉ in raw
                        // "lr=0x104e62934?" nên mọi abort của Avian đều vô danh và
                        // phải đoán. symbolicateAddr có sẵn symbol table (hàm static
                        // như crashHandler vẫn ra tên) → in luôn tên hàm ở đây để
                        // lý do abort hiện ra ngay trong crash log, không cần atos.
                        char lrSym[512];
                        if (!inGuest) {
                            symbolicateAddr((uintptr_t)savedLr, lrSym, sizeof(lrSym));
                        }
                        const bool guardLike =
                            slot56 > 0x100000000ULL && slot56 < 0x7fffffffffffULL;
                        int n2 = snprintf(sigline, sizeof(sigline),
                            "fp%02d: f=0x%llx lr=0x%llx %s slot56=0x%llx%s\n",
                            i, (unsigned long long)f, (unsigned long long)savedLr,
                            inGuest ? lrMod : lrSym, (unsigned long long)slot56,
                            guardLike ? " <-- guard?" : "");
                        crashWriteLine(fd, sigline, n2, sizeof(sigline));
                        // Chain hợp lệ: frame kế cao hơn, delta ≤ 64KB.
                        if (!(savedFp > f && savedFp - f < 0x10000)) break;
                        f = savedFp;
                    }
                }
#elif defined(__linux__)
                ucontext_t* uc = (ucontext_t*)ucontext;
                uint64_t pc = uc->uc_mcontext.pc;
                uint64_t lr = uc->uc_mcontext.regs[30];
                m = snprintf(sigline, sizeof(sigline),
                    "pc = 0x%llx\nlr = 0x%llx\n",
                    (unsigned long long)pc, (unsigned long long)lr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));

                char symPc[512], symLr[512];
                symbolicateAddr((uintptr_t)pc, symPc, sizeof(symPc));
                symbolicateAddr((uintptr_t)lr, symLr, sizeof(symLr));
                m = snprintf(sigline, sizeof(sigline), "pc_sym: %s\nlr_sym: %s\n", symPc, symLr);
                crashWriteLine(fd, sigline, m, sizeof(sigline));
#endif
            }
#endif

            (void)!write(fd, "\n--- backtrace ---\n", 19);
            writeBacktrace(fd);

            if (g_abortMessage[0]) {
                (void)!write(fd, "\n--- abort message ---\n", 23);
                (void)!write(fd, g_abortMessage, strlen(g_abortMessage));
                (void)!write(fd, "\n", 1);
            }

            (void)!write(fd, "\n--- log up to crash ---\n", 25);
            (void)!write(fd, g_crashBuf, (size_t)g_crashLen);

            // Dump đuôi stderr (lý do abort/fatal của avian — thường in ra đây
            // nhưng bị mất vì iOS không hiển thị stderr). open/lseek/read/write
            // đều async-signal-safe nên gọi được trong signal handler.
            {
                char errPath[1200];
                size_t dl = strlen(g_logDir);
                if (dl < sizeof(errPath) - 32) {
                    memcpy(errPath, g_logDir, dl);
                    memcpy(errPath + dl, "/stderr.log", 12);
                    int errFd = open(errPath, O_RDONLY);
                    if (errFd >= 0) {
                        const char* stderrHdr = "\n--- stderr tail (avian abort reason) ---\n";
                        (void)!write(fd, stderrHdr, strlen(stderrHdr));
                        off_t errSize = lseek(errFd, 0, SEEK_END);
                        const off_t maxTail = 4096;
                        if (errSize > maxTail) {
                            lseek(errFd, -maxTail, SEEK_END);
                        } else {
                            lseek(errFd, 0, SEEK_SET);
                        }
                        char errBuf[512];
                        ssize_t errN;
                        while ((errN = read(errFd, errBuf, sizeof(errBuf))) > 0) {
                            (void)!write(fd, errBuf, (size_t)errN);
                        }
                        (void)!write(fd, "\n", 1);
                        close(errFd);
                    }
                }
            }
            
            const char* traceStr = kudroid::bionic_shim_trace();
            if (traceStr && *traceStr) {
                (void)!write(fd, "\n--- bionic shim trace ---\n", 27);
                (void)!write(fd, traceStr, strlen(traceStr));
            }
            
            close(fd);
        }
    }

    // --- GENTLE CRASH SYSTEM ---
    // Đánh dấu trạng thái crash và trích xuất tối đa 30 dòng log cuối cùng
    // để UI Swift hiển thị cảnh báo "Whoops, the app crashed" mà không đóng app.
    g_hasCrashed.store(true);
    
    // Thu thập 30 dòng log cuối cùng từ g_crashBuf
    extractLastLines(g_crashBuf, (size_t)g_crashLen, g_lastCrashTail, sizeof(g_lastCrashTail), 30);
    
    // Nếu g_lastCrashTail quá ngắn, ghép thêm thông tin signal và PC
    if (strlen(g_lastCrashTail) < 30) {
        char fallbackSummary[512];
        snprintf(fallbackSummary, sizeof(fallbackSummary),
                 "[Crash Signal: %d] Fault at %p (Thread 0x%llx)",
                 sig, info ? info->si_addr : nullptr, (unsigned long long)(uintptr_t)pthread_self());
        strncat(g_lastCrashTail, fallbackSummary, sizeof(g_lastCrashTail) - strlen(g_lastCrashTail) - 1);
    }

    // Nếu crash xảy ra trên background thread (render thread, game thread, worker thread):
    // Không gọi hàm lock mutex trong signal handler để tránh deadlock/freeze!
#if defined(__APPLE__)
    const bool isBackground = !pthread_main_np();
#else
    const bool isBackground = g_mainThread != 0 && !pthread_equal(pthread_self(), g_mainThread);
#endif
    if (isBackground) {
        // Luồng background bị crash: giải phóng và tạm dừng luồng này để Swift timer phát hiện crash
        // và tự động hiển thị Gentle Crash modal mà không làm freeze/treo launcher.
        pause();
    } else {
        // Nếu crash ngay trên main thread, ghi nhận và kết thúc an toàn
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static void installCrashHandlers(void) {
    static bool installed = false;
    if (installed) return;
    installed = true;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    // Chạy handler trên stack riêng: khi crash là stack overflow (hoặc stack của
    // JVM thread quá nhỏ), handler chạy trên stack đã hỏng sẽ double-fault và
    // KHÔNG ghi được crash log — đúng triệu chứng "app chết im lặng". Cấp stack
    // tĩnh (không heap, an toàn trong signal context) rồi bật SA_ONSTACK.
    {
        // SIGSTKSZ không còn là hằng biên dịch trên glibc mới → dùng kích thước
        // cố định (64KB, thừa cho handler này) để mảng static hợp lệ ở mọi libc.
        armAltSignalStack();
        if (g_altStackArmed) {
            sa.sa_flags |= SA_ONSTACK;
        }
    }

    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGTRAP, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);

    // Android/bionic mặc định IGNORE SIGPIPE (write vào pipe/socket đã đóng trả
    // EPIPE thay vì giết process). Game .so tin vào hành vi này — nếu host giữ
    // SIGPIPE mặc định, chỉ cần game ghi log vào một pipe đã đóng là app chết
    // ngay mà không có crash log.
    ::signal(SIGPIPE, SIG_IGN);

#if defined(__APPLE__)
    // Bắt lý do abort trước khi nó xảy ra: ObjC exception chưa bắt + C++ terminate.
    NSSetUncaughtExceptionHandler(&kudroid_uncaught_objc_handler);
    std::set_terminate(&kudroid_terminate_handler);
#endif
}

extern "C" void kudroid_set_log_dir(const char* dir) {
    if (!dir) return;
    g_mainThread = pthread_self();

    // The host passes Documents; logs go into a subdirectory of it. Keeping the two
    // apart is the whole point — Documents also holds put_apk_here/, android_root/,
    // extracted_apk/ and micro_tests/, and eight log files scattered among them made
    // the directory hard to read and "which files are the logs" a thing to remember.
    strncpy(g_docsDir, dir, sizeof(g_docsDir) - 1);
    g_docsDir[sizeof(g_docsDir) - 1] = '\0';

    snprintf(g_logDir, sizeof(g_logDir), "%s/logs", g_docsDir);

    // Must exist before anything opens a file in it. The crash handler in particular
    // cannot create directories — it is restricted to async-signal-safe calls — so a
    // missing logs/ would silently lose every crash report.
    std::error_code mkdirError;
    std::filesystem::create_directories(g_logDir, mkdirError);
    if (mkdirError) {
        // Fall back to Documents rather than losing logs entirely. A cluttered
        // directory beats no diagnostics.
        fprintf(stderr, "[kudroid_core] cannot create %s (%s); logging to Documents\n",
                g_logDir, mkdirError.message().c_str());
        strncpy(g_logDir, g_docsDir, sizeof(g_logDir) - 1);
        g_logDir[sizeof(g_logDir) - 1] = '\0';
    }

    installCrashHandlers();

    static bool s_logDirInitialized = false;
    if (!s_logDirInitialized) {
        s_logDirInitialized = true;
        // Chỉ reset log đúng một lần khi app KuDroid mới mở, giữ nguyên log khi user ấn X / quay lại màn hình chính
        char aPath[1200];
        snprintf(aPath, sizeof(aPath), "%s/kudroid_android_logs.txt", g_logDir);
        FILE* afp = fopen(aPath, "w");
        if (afp) {
            // Dòng ĐẦU TIÊN của log chính luôn là build stamp — nhìn phát biết
            // ngay IPA đang chạy là commit nào, không cần mở file version riêng.
            fprintf(afp, "[KuDroidCore] Build: %s\n", kudroid_build_stamp());
            fclose(afp);
        }

        // Old runs wrote these straight into Documents. Move them aside so the top
        // level ends up clean even for someone upgrading, rather than leaving a
        // confusing mix of stale files next to the new logs/ directory.
        {
            static const char* kMoved[] = {
                "kudroid_android_logs.txt", "stderr.log", "kudroid_crash.log",
                "classes.log", "kudroid_version.txt", "kudroid_uninstall_debug.txt",
            };
            for (const char* name : kMoved) {
                const std::filesystem::path from =
                    std::filesystem::path(g_docsDir) / name;
                std::error_code ec;
                if (!std::filesystem::exists(from, ec)) continue;
                const std::filesystem::path to =
                    std::filesystem::path(g_logDir) / (std::string("old_") + name);
                std::filesystem::rename(from, to, ec);
                if (ec) std::filesystem::remove(from, ec);
            }
        }

#if defined(__APPLE__)
        // Redirect stderr (fd 2) vào file — dùng O_TRUNC đúng 1 lần khi mở app
        char errPath[1200];
        snprintf(errPath, sizeof(errPath), "%s/stderr.log", g_logDir);
        int errFd = open(errPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errFd >= 0) {
            dup2(errFd, STDERR_FILENO);
            setvbuf(stderr, nullptr, _IONBF, 0);
            close(errFd);
            // AFC (copy file qua USB/Finder) chỉ đọc được file có quyền
            // read cho other — umask có thể đã mask 0644 thành 0600.
            ::chmod(errPath, 0644);
            // Dòng đầu của stderr.log = build stamp — mọi log file đều
            // tự nhận diện được commit của bản IPA đang chạy.
            fprintf(stderr, "[kudroid_core] Build: %s\n", kudroid_build_stamp());
            fprintf(stderr, "[kudroid_core] log directory: %s\n", g_logDir);
        }
#endif
    }

    // Ghi stamp build ra file để user kiểm tra app đang chạy có phải bản mới
    // nhất không (trả lời "iPhone vẫn chạy app cũ?").
    const char* stamp = kudroid_build_stamp();
    writeLogFile("kudroid_version.txt", std::string(stamp) + "\n");

    // KuART's classes.log belongs with the other diagnostics, not in Documents.
    std::string classesLogPath = (std::filesystem::path(g_logDir) / "classes.log").string();
    kuart_set_missing_class_log_path(classesLogPath.c_str());
}

extern "C" void kudroid_set_documents_dir(const char* dir) {
    if (dir && dir[0] != '\0') {
        kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(dir);
        kudroid::PermissionManager::getInstance().init(dir);

        if (g_docsDir[0] == '\0') {
            strncpy(g_docsDir, dir, sizeof(g_docsDir) - 1);
            g_docsDir[sizeof(g_docsDir) - 1] = '\0';
        }

        // classes.log is a diagnostic, so it follows the log directory rather than
        // Documents. This used to point it back at Documents and undo whatever
        // kudroid_set_log_dir had chosen — the two functions disagreed and the last
        // one called won.
        if (g_logDir[0] != '\0') {
            const std::string classesLogPath =
                (std::filesystem::path(g_logDir) / "classes.log").string();
            kuart_set_missing_class_log_path(classesLogPath.c_str());
        }
    }
}

// con trỏ lớp metal toàn cục có thể truy cập bằng bionicshim
void* g_metalLayer = nullptr;
int g_metalLayerWidth = 1080;
int g_metalLayerHeight = 1920;
float g_metalLayerDensity = 3.0f;

// Đi qua pipeline log chuẩn (stdout + kudroid_android_logs.txt + crash buffer)
// — định nghĩa trong SyscallShim.cpp, dùng chung với GraphicsShim.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);

// GraphicsShim.cpp — ANGLE first-touch phải trên main thread (xem comment ở
// kudroid_set_metal_layer).
extern "C" void kudroid_gpu_warmup_egl(void);
extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface);

extern "C" void kudroid_set_metal_layer(void* layer, int width, int height, float density) {
    g_metalLayer = layer;
    g_metalLayerWidth = width;
    g_metalLayerHeight = height;
    g_metalLayerDensity = density > 0.0f ? density : g_metalLayerDensity;
    // Log kích thước nhận từ Swift — trước đây không log gì nên width/height
    // sai (0/âm) chỉ lộ ra ở ANativeWindow_lock (hoặc không bao giờ nếu game
    // không dùng lock).
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "kudroid_set_metal_layer(layer=%p, size=%dx%d, density=%.2f)",
                  layer, width, height, density);
    kudroid_android_log_message(2, "KuDroidGPU", buf);

    // Resize the software canvas to the real surface. The Java-side Canvas asks for
    // these numbers via native_getSurfaceWidth/Height, so layout is computed for the
    // actual screen instead of the old hardcoded 1080x1920.
    if (width > 0 && height > 0) {
        kudroid::JavaCanvasRenderer::getInstance().init(width, height);
    }

    // Để render thread tự khởi tạo EGL display sạch sẽ từ đầu
    // kudroid_gpu_warmup_egl();
}

extern "C" const char* kudroid_vfs_self_test_log(void) {
    const std::string log = kudroid::run_vfs_self_test();
    writeLogFile("kudroid_vfs_selftest.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_vfs_extended_test_log(void) {
    const std::string log = kudroid::run_vfs_extended_test();
    writeLogFile("kudroid_vfs_extended_test.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

extern "C" const char* kudroid_install_apk(const char* apkPath) {
    std::string log = "[kudroid_core] ===== APK Install =====\n";
    if (!apkPath || !*apkPath) {
        log += "[kudroid_apk] ERROR: APK path is empty\n";
    } else {
        const std::filesystem::path source(apkPath);
        std::string pkgId = kudroid::APKExtractor::get_package_name(source.string());
        if (pkgId.empty()) {
            pkgId = source.stem().string();
            auto uIdx = pkgId.find('_');
            if (uIdx != std::string::npos) {
                pkgId = pkgId.substr(0, uIdx);
            }
        }
        for (char& character : pkgId) {
            if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-' || character == '.')) {
                character = '_';
            }
        }
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path appDir = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / pkgId;
        log += "[kudroid_apk] APK: " + source.string() + "\n";
        log += "[kudroid_apk] Target Android Package: " + pkgId + "\n";
        log += "[kudroid_apk] Target extraction directory: " + appDir.string() + "\n";
        bool extractedOk = false;
        if (kudroid::APKExtractor::is_bundle_container(source.string())) {
            log += "[kudroid_apk] Split-APK bundle detected (.xapk/.apks/.apkm), merging splits...\n";
            extractedOk = kudroid::APKExtractor::extract_bundle(source.string(), appDir.string());
        } else {
            extractedOk = kudroid::APKExtractor::extract_apk(source.string(), appDir.string());
        }
        if (extractedOk) {
            // pkgId phía trên đoán từ tên file APK khi get_package_name() không đọc
            // được manifest (bundle container: manifest thật nằm trong base APK bên
            // trong, không ở top level). Sau khi giải nén, app_info.json đã chứa
            // package thật do parseAxml của base manifest → dùng nó làm nguồn duy
            // nhất và đổi tên thư mục, để data/app và dalvik-cache luôn theo
            // package ID chứ không phải tên file APK.
            std::string effectiveAppName = pkgId;
            std::filesystem::path effectiveAppDir = appDir;
            {
                std::ifstream infoFile(appDir / "app_info.json");
                std::string line;
                while (std::getline(infoFile, line)) {
                    const auto pos = line.find("\"package\": \"");
                    if (pos == std::string::npos) continue;
                    const auto start = pos + 12;
                    const auto end = line.find('"', start);
                    if (end == std::string::npos) break;
                    const std::string realPkg = line.substr(start, end - start);
                    if (realPkg.empty() || realPkg == pkgId) break;
                    const std::filesystem::path target =
                        std::filesystem::path(remapper.androidRoot()) / "data/app" / realPkg;
                    std::error_code renameEc;
                    std::filesystem::remove_all(target, renameEc);
                    std::filesystem::rename(appDir, target, renameEc);
                    if (!renameEc) {
                        effectiveAppName = realPkg;
                        effectiveAppDir = target;
                        log += "[kudroid_apk] Normalized install dir to package ID: " + realPkg + "\n";
                    }
                    break;
                }
            }

            log += "[kudroid_apk] APK extracted successfully to " + effectiveAppName + "\n";
        } else {
            log += "[kudroid_apk] INSTALL FAILED: " +
                   kudroid::APKExtractor::lastError() + "\n";
        }
    }
    writeLogFile("kudroid_apk_install.txt", log);
    char* result = static_cast<char*>(std::malloc(log.size() + 1));
    if (result) std::memcpy(result, log.c_str(), log.size() + 1);
    return result;
}

#if defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#include <libkern/OSCacheControl.h>
#include <TargetConditionals.h>

// csops() là một api riêng tư nhưng ổn định; được sử dụng để đọc trạng thái chữ ký mã của quá trình.
// cs_debugged được đặt khi đường dẫn jit (ký mã động) hoạt động
// dưới livecontainer / trình gỡ lỗi, điều này cho phép các trang prot_exec chạy.
extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
#ifndef CS_OPS_STATUS
#define CS_OPS_STATUS 0
#endif
#ifndef CS_DEBUGGED
#define CS_DEBUGGED 0x10000000
#endif
#endif

// trả về 1 nếu jit (bộ nhớ có thể thực thi) có vẻ khả dụng, ngược lại trả về 0.
extern "C" int kudroid_is_jit_enabled(void) {
#if defined(__APPLE__)
    // 1. Kiểm tra CS_DEBUGGED (Trình gỡ lỗi: AltStore, SideStore, Sideloadly, Xcode, StikDebug, Jitterbug)
    unsigned int flags = 0;
    if (csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) == 0) {
        if (flags & CS_DEBUGGED) {
            return 1;
        }
    }

    // 2. Kiểm tra môi trường TrollStore / Jailbreak qua đường dẫn an toàn (không gọi mã máy động để tránh SIGKILL)
    if (access("/Applications/TrollStore.app", F_OK) == 0 ||
        access("/var/mobile/Library/TrollStore", F_OK) == 0 ||
        getenv("TROLLSTORE_ENABLED") != nullptr) {
        return 1;
    }
    return 0; // Hoàn toàn không có JIT!
#else
    return 1;
#endif
}

static int kudroid_jit_available(void) {
    return kudroid_is_jit_enabled();
}

extern "C" const char* kudroid_jit_status(void) {
    const char* text = kudroid_is_jit_enabled()
        ? "JIT: Enabled"
        : "JIT: Disabled";
    char* result = (char*)malloc(strlen(text) + 1);
    if (result) memcpy(result, text, strlen(text) + 1);
    return result;
}

static std::atomic<int> g_requestedOrientation{-1}; // -1=Unspecified, 0=Landscape, 1=Portrait

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_notify_orientation_change(int orientation) { (void)orientation; }
#else
extern "C" void kudroid_notify_orientation_change(int orientation) { (void)orientation; }
#endif

extern "C" void kudroid_set_requested_orientation(int orientation) {
    g_requestedOrientation.store(orientation);
    kudroid_notify_orientation_change(orientation);
    fprintf(stderr, "[KuDroidCore] Screen orientation requested: %d (%s)\n",
            orientation,
            (orientation == 0 || orientation == 6 || orientation == 8) ? "Landscape" :
            (orientation == 1 || orientation == 7 || orientation == 9) ? "Portrait" : "Sensor/Auto");
}

extern "C" int kudroid_get_requested_orientation(void) {
    return g_requestedOrientation.load();
}

extern "C" JNIEXPORT void JNICALL Java_android_app_Activity_setRequestedOrientation_1native(JNIEnv* env, jclass clazz, jint orientation) {
    (void)env; (void)clazz;
    kudroid_set_requested_orientation(orientation);
}

extern "C" void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height);

static std::atomic<bool> s_isApkRunning{false};

extern "C" void kudroid_unbind_metal_layer(void) {
    g_metalLayer = nullptr;
    g_metalLayerWidth = 0;
    g_metalLayerHeight = 0;
    s_isApkRunning.store(false);
    kudroid_android_log_message(2, "KuDroidGPU", "kudroid_unbind_metal_layer: GPU surface unbound cleanly.");
}

static uint32_t* s_softwareFrameBuffer = nullptr;
static size_t s_softwareFrameBufferSize = 0;
static int s_softwareWidth = 1080;
static int s_softwareHeight = 1920;
static std::mutex s_canvasMutex;

static void ensure_software_framebuffer() {
    int w = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    int h = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
    s_softwareWidth = w;
    s_softwareHeight = h;
    size_t needed = (size_t)w * h * sizeof(uint32_t);
    if (!s_softwareFrameBuffer || needed > s_softwareFrameBufferSize) {
        void* nb = std::realloc(s_softwareFrameBuffer, needed);
        if (nb) {
            s_softwareFrameBuffer = static_cast<uint32_t*>(nb);
            s_softwareFrameBufferSize = needed;
            std::memset(s_softwareFrameBuffer, 0, needed);
        }
    }
}

// Chuyển ARGB (Android format) sang RGBA / BGRA (iOS Metal format)
static inline uint32_t argb_to_rgba(uint32_t argb) {
    uint32_t a = (argb >> 24) & 0xFF;
    uint32_t r = (argb >> 16) & 0xFF;
    uint32_t g = (argb >> 8) & 0xFF;
    uint32_t b = (argb) & 0xFF;
    return (r) | (g << 8) | (b << 16) | (a << 24);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawColor(JNIEnv* env, jclass clazz, jint color) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;
    uint32_t rgba = argb_to_rgba(static_cast<uint32_t>(color));
    size_t total = (size_t)s_softwareWidth * s_softwareHeight;
    for (size_t i = 0; i < total; ++i) {
        s_softwareFrameBuffer[i] = rgba;
    }
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawRect(JNIEnv* env, jclass clazz, jfloat left, jfloat top, jfloat right, jfloat bottom, jint color) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;

    int x0 = std::max(0, std::min(s_softwareWidth - 1, static_cast<int>(left)));
    int y0 = std::max(0, std::min(s_softwareHeight - 1, static_cast<int>(top)));
    int x1 = std::max(0, std::min(s_softwareWidth - 1, static_cast<int>(right)));
    int y1 = std::max(0, std::min(s_softwareHeight - 1, static_cast<int>(bottom)));
    uint32_t rgba = argb_to_rgba(static_cast<uint32_t>(color));

    for (int y = y0; y <= y1; ++y) {
        uint32_t* row = s_softwareFrameBuffer + y * s_softwareWidth;
        for (int x = x0; x <= x1; ++x) {
            row[x] = rgba;
        }
    }
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawText(JNIEnv* env, jclass clazz, jstring text, jfloat x, jfloat y, jint color, jfloat textSize) {
    (void)env; (void)clazz; (void)text; (void)textSize;
    Java_android_graphics_Canvas_native_1drawRect(env, clazz, x, y - 16.0f, x + 250.0f, y + 4.0f, color);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1drawBitmap(JNIEnv* env, jclass clazz, jintArray pixels, jint width, jint height, jfloat x, jfloat y) {
    (void)env; (void)clazz;
    if (!pixels || width <= 0 || height <= 0) return;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    ensure_software_framebuffer();
    if (!s_softwareFrameBuffer) return;

    jint* src = env->GetIntArrayElements(pixels, nullptr);
    if (!src) return;

    int dstX = static_cast<int>(x);
    int dstY = static_cast<int>(y);

    for (int r = 0; r < height; ++r) {
        int py = dstY + r;
        if (py < 0 || py >= s_softwareHeight) continue;
        for (int c = 0; c < width; ++c) {
            int px = dstX + c;
            if (px < 0 || px >= s_softwareWidth) continue;
            uint32_t pixel = static_cast<uint32_t>(src[r * width + c]);
            s_softwareFrameBuffer[py * s_softwareWidth + px] = argb_to_rgba(pixel);
        }
    }

    env->ReleaseIntArrayElements(pixels, src, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL Java_android_graphics_Canvas_native_1flush(JNIEnv* env, jclass clazz) {
    (void)env; (void)clazz;
    std::lock_guard<std::mutex> lock(s_canvasMutex);
    if (g_metalLayer && s_softwareFrameBuffer && s_softwareWidth > 0 && s_softwareHeight > 0) {
        kudroid_blit_canvas_to_layer(g_metalLayer, s_softwareFrameBuffer, s_softwareWidth, s_softwareHeight);
    }
}

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_trigger_haptic(int intensity) { (void)intensity; }
#else
extern "C" void kudroid_trigger_haptic(int intensity) { (void)intensity; }
#endif

extern "C" void kudroid_vibrate(int intensity) {
    kudroid_trigger_haptic(intensity);
    fprintf(stderr, "[KuDroidCore] kudroid_vibrate(intensity=%d: %s)\n",
            intensity,
            intensity == 1 ? "Light" : (intensity == 2 ? "Medium" : "Heavy"));
}

extern "C" JNIEXPORT void JNICALL Java_android_os_Vibrator_kudroid_1vibrate_1native(JNIEnv* env, jclass clazz, jint intensity) {
    (void)env; (void)clazz;
    kudroid_vibrate(intensity);
}

static std::atomic<int> s_keepScreenOn{1}; // Mặc định khi chạy app là 1 (No Sleep)

extern "C" void kudroid_set_keep_screen_on(int keepOn) {
    s_keepScreenOn.store(keepOn != 0 ? 1 : 0);
    fprintf(stderr, "[KuDroidCore] kudroid_set_keep_screen_on(%d)\n", keepOn != 0 ? 1 : 0);
}

extern "C" int kudroid_get_keep_screen_on(void) {
    return s_keepScreenOn.load();
}

extern "C" JNIEXPORT void JNICALL Java_android_view_Window_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_android_view_View_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_android_os_PowerManager_00024WakeLock_setKeepScreenOnNative(JNIEnv* env, jclass clazz, jboolean keepOn) {
    (void)env; (void)clazz;
    kudroid_set_keep_screen_on(keepOn ? 1 : 0);
}

// ── soft keyboard ────────────────────────────────────────────────────────────
//
// KuDroid has no keyboard of its own; iOS does. The guest's
// InputMethodManager.showSoftInput reaches kudroid_show_soft_input, which forwards
// to a callback the Swift side registers — kudroid_core is a static library and
// cannot touch UIKit, so the view that becomes first responder has to call back in.
//
// Text goes the other way, from the host keyboard into the guest's focused
// InputConnection, through kuart_dispatch_text_input.

static kudroid_soft_input_show_cb s_softInputShow = nullptr;
static kudroid_soft_input_hide_cb s_softInputHide = nullptr;
static std::atomic<int> s_softInputVisible{0};

extern "C" void kudroid_set_soft_input_callbacks(kudroid_soft_input_show_cb show,
                                                kudroid_soft_input_hide_cb hide) {
    s_softInputShow = show;
    s_softInputHide = hide;
    fprintf(stderr, "[KuDroidCore] soft input callbacks %s\n",
            (show != nullptr || hide != nullptr) ? "registered" : "cleared");
}

extern "C" int kudroid_show_soft_input(int flags) {
    fprintf(stderr, "[KuDroidCore] kudroid_show_soft_input(flags=0x%x) host=%s\n",
            flags, s_softInputShow != nullptr ? "yes" : "none");
    if (s_softInputShow == nullptr) return 0;
    s_softInputShow(flags);
    return 1;
}

extern "C" int kudroid_hide_soft_input(void) {
    fprintf(stderr, "[KuDroidCore] kudroid_hide_soft_input() host=%s\n",
            s_softInputHide != nullptr ? "yes" : "none");
    if (s_softInputHide == nullptr) return 0;
    s_softInputHide();
    return 1;
}

extern "C" int kudroid_is_soft_input_visible(void) {
    return s_softInputVisible.load();
}

extern "C" void kudroid_set_soft_input_visible(int visible) {
    const int v = visible != 0 ? 1 : 0;
    if (s_softInputVisible.exchange(v) != v) {
        fprintf(stderr, "[KuDroidCore] soft input now %s\n", v ? "visible" : "hidden");
    }
}

extern "C" void kudroid_dispatch_text_input(const char* utf8) {
    if (utf8 == nullptr || utf8[0] == '\0') return;
    kuart_dispatch_text_input(utf8);
}

extern "C" void kudroid_dispatch_delete_backward(void) {
    kuart_dispatch_delete_backward();
}

#include "kudroid/KuArtRuntime.h"

// --- định nghĩa nativeactivity ---

struct ANativeActivityCallbacks {
    void* onStart;
    void* onResume;
    void* onSaveInstanceState;
    void* onPause;
    void* onStop;
    void* onDestroy;
    void* onWindowFocusChanged;
    void* onNativeWindowCreated;
    void* onNativeWindowResized;
    void* onNativeWindowRedrawNeeded;
    void* onNativeWindowDestroyed;
    void* onInputQueueCreated;
    void* onInputQueueDestroyed;
    void* onContentRectChanged;
    void* onConfigurationChanged;
    void* onLowMemory;
};

struct ANativeActivity {
    ANativeActivityCallbacks* callbacks;
    JavaVM* vm;
    JNIEnv* env;
    jclass clazz;
    const char* internalDataPath;
    const char* externalDataPath;
    int32_t sdkVersion;
    void* instance;
    void* assetManager;
    const char* obbPath;
};


// Tự kiểm tra KuART: nạp framework.dex nhúng rồi thử JNI cả hai chiều. Tham số
// giữ lại cho tương thích ABI với vỏ Swift (trước là đường dẫn rt.jar của Avian).
extern "C" char* kudroid_test_jvm(const char* unused_path) {
    (void)unused_path;
    std::string log;
    appendTestHeader(log, "KuART Integration Test", "N/A");
    installCrashHandlers();

    log += "[kudroid_core] Phase: init KuART\n";
    kudroid::bionic_shim_reset_trace();

    static std::string* g_kuart_test_log = &log;
    g_kuart_test_log = &log;
    kuart_set_log_callback([](const char* msg) {
        if (g_kuart_test_log) {
            *g_kuart_test_log += "[KuART] ";
            *g_kuart_test_log += msg;
            *g_kuart_test_log += "\n";
        }
    });

    // app_dir rỗng = chỉ nạp framework.dex nhúng.
    if (!kuart_init("")) {
        log += "[kudroid_core] ERROR: kuart_init failed: " + std::string(kuart_last_error()) + "\n";
        kuart_set_log_callback(nullptr);
        g_kuart_test_log = nullptr;
        return strdup(log.c_str());
    }
    log += "[kudroid_core] KuART DEX loaded: " + std::to_string(kuart_num_dex_files()) + "\n";

    JavaVM* vm = kuart_get_javavm();
    if (!vm) {
        log += "[kudroid_core] ERROR: JavaVM is null!\n";
        kuart_set_log_callback(nullptr);
        g_kuart_test_log = nullptr;
        return strdup(log.c_str());
    }

    JNIEnv* env = nullptr;
    kuart_get_env(vm, reinterpret_cast<void**>(&env), 0);

    if (env) {
        log += "[kudroid_core] Phase: testing JNI FindClass\n";
        // ActivityThread là class mà đường khởi động thật sự dùng — nó load được
        // nghĩa là framework nhúng hợp lệ và class linker chạy đúng.
        for (const char* name : {"android/app/ActivityThread", "android/app/Activity",
                                 "android/os/Looper", "android/util/Log"}) {
            jclass c = env->FindClass(name);
            log += std::string("[kudroid_core] ") + (c ? "SUCCESS" : "FAILED ") +
                   ": FindClass(" + name + ")\n";
            if (env->ExceptionCheck()) env->ExceptionClear();
        }

        jclass at = env->FindClass("android/app/ActivityThread");
        if (at) {
            jmethodID main = env->GetStaticMethodID(at, "main", "([Ljava/lang/String;)V");
            log += std::string("[kudroid_core] ") + (main ? "SUCCESS" : "FAILED ") +
                   ": GetStaticMethodID(ActivityThread.main)\n";
        }

        jstring testStr = env->NewStringUTF("Hello KuART");
        if (testStr) {
            const char* utf = env->GetStringUTFChars(testStr, nullptr);
            log += "[kudroid_core] SUCCESS: Created JNI string: ";
            log += utf ? utf : "null";
            log += "\n";
            env->ReleaseStringUTFChars(testStr, utf);
        }
        log += "[kudroid_core] classes resolved: " +
               std::to_string(kuart_num_loaded_classes()) + "\n";
    } else {
        log += "[kudroid_core] ERROR: Failed to get JNIEnv!\n";
    }

    log += "[kudroid_core] Phase: shutdown KuART\n";
    kuart_shutdown();
    kuart_set_log_callback(nullptr);
    g_kuart_test_log = nullptr;

    log += "[kudroid_core] KuART test completed.\n";
    return strdup(log.c_str());
}

// Guest .so mappings PHẢI sống bằng tuổi process — đúng ngữ nghĩa dlopen trên
// Android (libs ở lại trong process cho tới khi app chết). Bằng chứng log máy
// thật: mọi crash triangle từ đầu đều là SIGABRT trong libtriangle_gles.so+
// 0x4xxx NGAY SAU khi run_apk in dòng cuối "ANativeActivity_onCreate not found"
// — render thread guest chạy SONG SONG với run_apk (spawn từ JNI_OnLoad), còn
// manager trước đây là biến LOCAL nên khi run_apk return, destructor ElfLoader
// munmap toàn bộ guest .so trong lúc render thread vẫn đang thực thi bên trong
// → thực thi vùng nhớ đã unmap → abort. Warm-up ANGLE các vòng trước chỉ làm
// render thread tới xa hơn TRƯỚC khi munmap (race) nên crash dời dần — không
// phải fix gốc.
static kudroid::LibraryManager& globalLibraryManager() {
    static kudroid::LibraryManager instance;
    return instance;
}

// Hook tra symbol guest — định nghĩa trong SyscallShim.cpp, bionic_dlsym dùng
// khi handle là DUMMY_HANDLE (dlopen("libc.so") v.v.).
extern "C" {
extern void* (*kudroid_guest_symbol_lookup)(const char* name);
}

extern "C" const char* kudroid_run_apk(const char* appName) {
    if (s_isApkRunning.exchange(true)) {
        kudroid_android_log_message(3, "kudroid_core", "kudroid_run_apk: APK is already running in background, ignoring duplicate launch request.");
        return strdup("[kudroid_core] APK is already running.\n");
    }

    // Reset/truncate logs để mỗi phiên chạy app luôn ghi đè log mới hoàn toàn (không chồng lên nhau)
    if (g_logDir[0] != '\0') {
        char aPath[1200];
        snprintf(aPath, sizeof(aPath), "%s/kudroid_android_logs.txt", g_logDir);
        FILE* afp = fopen(aPath, "w");
        if (afp) {
            // Build stamp ở dòng đầu tiên của log phiên chạy APK mới.
            fprintf(afp, "[KuDroidCore] Build: %s\n", kudroid_build_stamp());
            fclose(afp);
        }

        snprintf(aPath, sizeof(aPath), "%s/kudroid_crash.log", g_logDir);
        FILE* cfp = fopen(aPath, "w");
        if (cfp) fclose(cfp);

#if defined(__APPLE__)
        char errPath[1200];
        snprintf(errPath, sizeof(errPath), "%s/stderr.log", g_logDir);
        int errFd = open(errPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errFd >= 0) {
            dup2(errFd, STDERR_FILENO);
            setvbuf(stderr, nullptr, _IONBF, 0);
            close(errFd);
            // AFC (kdb dump / Finder) chỉ đọc được file có quyền read-other.
            ::chmod(errPath, 0644);
            fprintf(stderr, "[kudroid_core] Build: %s\n", kudroid_build_stamp());
        }
#endif
    }

    std::string log;
    appendTestHeader(log, "Run APK Native Libraries", appName);
    kudroid::bionic_shim_reset_trace();
    log += "[kudroid_core] Phase: init LibraryManager\n";

    if (!appName || !*appName) {
        log += "[kudroid_core] ERROR: null or empty app name\n";
    } else {
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        std::string resolvedAppName = appName;
        std::filesystem::path appDir = std::filesystem::path(remapper.androidRoot()) / "data/app" / resolvedAppName;

        // ── Chuẩn hóa tên app = package ID thật ───────────────────────────
        // Thư mục cài đặt thường mang tên FILE APK tải về (vd
        // "Minecraft_PE_26.30_BANDISHARE") trong khi Android thật định danh
        // app bằng package ID ("com.mojang.minecraftpe"). Đọc manifest NGAY
        // TỪ ĐẦU để đổi tên thư mục + di chuyển dalvik-cache sang tên chuẩn:
        // mọi đường dẫn runtime (dalvik-cache, data/data, sdcard/Android/data)
        // khớp Android thật và nhất quán dù tải lại APK với tên file khác.
        {
            const auto mfPath = appDir / "AndroidManifest.xml";
            std::string pkgId;
            if (std::filesystem::exists(mfPath)) {
                std::ifstream mf(mfPath, std::ios::binary);
                if (mf) {
                    std::vector<std::uint8_t> axml(
                        (std::istreambuf_iterator<char>(mf)),
                        std::istreambuf_iterator<char>());
                    kudroid::ManifestInfo mi =
                        kudroid::APKExtractor::parse_manifest(axml.data(), axml.size());
                    if (mi.packageName.empty() && axml.size() > 4 && axml[0] == '<') {
                        mi = kudroid::APKExtractor::parse_manifest_text(
                            reinterpret_cast<const char*>(axml.data()), axml.size());
                    }
                    pkgId = mi.packageName;
                }
            }

            if (!pkgId.empty() && pkgId != resolvedAppName) {
                const auto cleanAppDir =
                    std::filesystem::path(remapper.androidRoot()) / "data/app" / pkgId;
                std::error_code renEc;
                bool movedApp = false;
                if (!std::filesystem::exists(cleanAppDir)) {
                    std::filesystem::rename(appDir, cleanAppDir, renEc);
                    movedApp = !renEc;
                }
                // Dalvik-cache cũ của đường dex2jar phải theo tên mới để bước
                // dọn dẹp sau này tìm thấy và xóa được.
                const auto oldCache = std::filesystem::path(remapper.androidRoot()) /
                                      "data/dalvik-cache" / resolvedAppName;
                const auto newCache = std::filesystem::path(remapper.androidRoot()) /
                                      "data/dalvik-cache" / pkgId;
                if (std::filesystem::exists(oldCache) && !std::filesystem::exists(newCache)) {
                    std::error_code cacheEc;
                    std::filesystem::rename(oldCache, newCache, cacheEc);
                }
                if (movedApp) {
                    resolvedAppName = pkgId;
                    appDir = cleanAppDir;
                } else if (std::filesystem::exists(cleanAppDir)) {
                    // Đã có thư mục chuẩn từ trước (cài đè) — giữ nguyên bố cục cũ.
                }
            }
        }

        // Fallback cũ: nếu manifest không đọc được, thử app_info.json do
        // extractor ghi lúc cài đặt (chứa package ID chuẩn).
        if (std::filesystem::exists(appDir)) {
            std::filesystem::path infoPath = appDir / "app_info.json";
            if (std::filesystem::exists(infoPath)) {
                std::ifstream f(infoPath);
                std::string line;
                while (std::getline(f, line)) {
                    auto pos = line.find("\"package\": \"");
                    if (pos != std::string::npos) {
                        auto start = pos + 12;
                        auto end = line.find("\"", start);
                        if (end != std::string::npos) {
                            std::string pkg = line.substr(start, end - start);
                            if (!pkg.empty() && pkg != resolvedAppName) {
                                std::filesystem::path cleanTarget = std::filesystem::path(remapper.androidRoot()) / "data/app" / pkg;
                                std::error_code ec;
                                std::filesystem::rename(appDir, cleanTarget, ec);
                                if (!ec) {
                                    resolvedAppName = pkg;
                                    appDir = cleanTarget;
                                }
                            }
                        }
                    }
                }
            }
        } else {
            auto uIdx = resolvedAppName.find('_');
            if (uIdx != std::string::npos) {
                std::string base = resolvedAppName.substr(0, uIdx);
                std::filesystem::path baseDir = std::filesystem::path(remapper.androidRoot()) / "data/app" / base;
                if (std::filesystem::exists(baseDir)) {
                    resolvedAppName = base;
                    appDir = baseDir;
                }
            }
        }

        const std::filesystem::path libDir = appDir / "lib/arm64-v8a";
        const std::filesystem::path assetsDir = appDir / "assets";
        kudroid_set_assets_dir(assetsDir.string().c_str());
        
        auto appendAndEcho = [&](const std::string& line) {
            log += line + "\n";
            std::fprintf(stderr, "%s\n", line.c_str());
            kudroid_android_log_message(2, "KuDroidCore", line.c_str());
            mirrorCrash(log);
        };
        
        appendAndEcho("[kudroid_core] Scanning library directory: " + libDir.string());
        
        if (!std::filesystem::exists(libDir)) {
            appendAndEcho("[kudroid_core] ERROR: Library directory does not exist: " + libDir.string());
        } else {
            kudroid::LibraryManager& manager = globalLibraryManager();

            // Cài hook tra symbol guest cho bionic_dlsym(DUMMY_HANDLE, ...):
            // dlopen("libc.so") trả handle giả nhưng dlsym(handle, "hàm") trên
            // Android thật vẫn resolve được — không cài hook thì init code của
            // guest nhận nullptr rồi gọi → SIGSEGV pc=0x0 (crash libmaesdk).
            kudroid_guest_symbol_lookup = [](const char* name) -> void* {
                return globalLibraryManager().resolveGlobalSymbol(name);
            };

            // KuART phải sẵn sàng TRƯỚC khi dlopen bất kỳ .so nào: static
            // initializer của libminecraftpe.so gọi JNI_GetCreatedJavaVMs, nếu
            // chưa có VM thì nó tự dựng state sai rồi không sửa lại được.
            kuart_set_log_callback([](const char* msg) {
                kudroid_android_log_message(4, "KuART", msg);
                std::fprintf(stderr, "[KuART] %s\n", msg);
            });
            kuart_set_symbol_lookup([](const char* symbol) -> void* {
                return globalLibraryManager().resolveGlobalSymbol(symbol);
            });

            if (kuart_init(appDir.string().c_str())) {
                appendAndEcho("[kudroid_core] KuART ready: " +
                              std::to_string(kuart_num_dex_files()) + " DEX loaded");
            } else {
                appendAndEcho("[kudroid_core] ERROR: KuART init failed: " +
                              std::string(kuart_last_error()));
            }

            std::vector<std::string> soFiles;
            for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
                if (entry.path().extension() == ".so") {
                    soFiles.push_back(entry.path().string());
                }
            }
            
            if (soFiles.empty()) {
                appendAndEcho("[kudroid_core] WARNING: No .so files found in " + libDir.string());
            } else {
                for (const auto& soPath : soFiles) {
                    appendAndEcho("[kudroid_core] Attempting to load: " + soPath);
                    if (!manager.loadRecursive(soPath.c_str())) {
                        appendAndEcho("[kudroid_core] LOAD FAILED for " + soPath + ": " + manager.lastError());
                    } else {
                        appendAndEcho("[kudroid_core] LOAD SUCCESS for " + soPath);
                    }
                }
                
                appendAndEcho("[kudroid_core] Total loaded libraries: " + std::to_string(manager.libraries().size()));
                for (const auto& pair : manager.libraries()) {
                    char mapLine[256];
                    snprintf(mapLine, sizeof(mapLine), "  %s -> %p", pair.first.c_str(), pair.second->baseAddress());
                    appendAndEcho(mapLine);
                }

                JavaVM* jvm = kuart_get_javavm();
                if (!jvm) {
                    appendAndEcho("[kudroid_core] ERROR: KuART failed to initialize.");
                } else {
                    char jvmLine[128];
                    snprintf(jvmLine, sizeof(jvmLine), "[kudroid_core] KuART ready (JavaVM=%p).", (void*)jvm);
                    appendAndEcho(jvmLine);

                    auto native_activity_create = reinterpret_cast<void (*)(ANativeActivity*, void*, size_t)>(
                        manager.resolveAppSymbol("ANativeActivity_onCreate")
                    );

                    kuart_set_load_library_callback([](const char* libname) -> int {
                        static std::set<std::string> s_loadedJniOnLoads;
                if (!libname || !*libname) return 0;
                std::string name = libname;
                std::string filename = name;
                if (filename.find(".so") == std::string::npos) {
                    filename = "lib" + filename + ".so";
                }
                kudroid::LibraryManager& manager = globalLibraryManager();
                void* sym = manager.resolveSymbolInLib(filename, "JNI_OnLoad");
                if (!sym) sym = manager.resolveSymbolInLib(name, "JNI_OnLoad");
                if (sym) {
                    if (s_loadedJniOnLoads.insert(filename).second) {
                        JavaVM* jvm = kuart_get_javavm();
                        if (jvm) {
                            bionic_init_main_thread_tls();
                            char msg[256];
                            snprintf(msg, sizeof(msg), "[kudroid_core] Invoking JNI_OnLoad in %s (System.loadLibrary)", filename.c_str());
                            kudroid_android_log_message(4, "kudroid_core", msg);
                            std::fprintf(stderr, "%s\n", msg);

                            auto jni_onload = reinterpret_cast<jint (*)(JavaVM*, void*)>(sym);
                            jint version = 0;
                            const int guardRc = kudroid_call_jni_onload_guarded(jni_onload, jvm, &version);
                            if (guardRc == 0) {
                                snprintf(msg, sizeof(msg), "[kudroid_core] JNI_OnLoad(%s) returned version: %d", filename.c_str(), version);
                                kudroid_android_log_message(4, "kudroid_core", msg);
                                std::fprintf(stderr, "%s\n", msg);
                            } else if (guardRc < 0) {
                                snprintf(msg, sizeof(msg), "[kudroid_core] WARNING: Native exception in JNI_OnLoad for %s", filename.c_str());
                                kudroid_android_log_message(5, "kudroid_core", msg);
                                std::fprintf(stderr, "%s\n", msg);
                                const std::string report =
                                    describeJniGuardFault(filename, guardRc);
                                std::fputs(report.c_str(), stderr);
                                appendCrashLogFile(report);
                            } else {
                                snprintf(msg, sizeof(msg), "[kudroid_core] WARNING: JNI_OnLoad in %s raised fatal signal %d", filename.c_str(), guardRc);
                                kudroid_android_log_message(5, "kudroid_core", msg);
                                std::fprintf(stderr, "%s\n", msg);
                                // The guard leaves the crash-log path unreached, so
                                // report the captured state here. Without it a
                                // swallowed fault was one WARNING line with no pc,
                                // no fault address and no stack — nothing to work
                                // from. Goes to kudroid_crash.log as well, where
                                // every other fault in this process is recorded.
                                const std::string report =
                                    describeJniGuardFault(filename, guardRc);
                                std::fputs(report.c_str(), stderr);
                                appendCrashLogFile(report);
                            }

                            // JNI requires native code to check and clear pending
                            // exceptions before returning to Java. Libraries that
                            // ignore the rule leave one in flight, and the
                            // interpreter then attributes it to the Java call that
                            // triggered the load: an exception raised inside a
                            // JNI_OnLoad callback came back out of
                            // System.loadLibrary("PlayFabMultiplayer"), which
                            // Minecraft only guards with
                            // catch(UnsatisfiedLinkError), so MainActivity.<clinit>
                            // died on an unrelated ArrayIndexOutOfBoundsException.
                            //
                            // Done on every exit path, not just the clean one: a
                            // library that aborted mid-callback is even more likely
                            // to have left one behind. The description carries the
                            // Java stack trace so the discarded exception stays
                            // diagnosable instead of vanishing.
                            const char* leaked = nullptr;
                            if (kuart_take_pending_exception(&leaked)) {
                                snprintf(msg, sizeof(msg),
                                         "[kudroid_core] JNI_OnLoad(%s) left a pending Java exception; cleared",
                                         filename.c_str());
                                kudroid_android_log_message(5, "kudroid_core", msg);
                                // The description carries a multi-line stack trace,
                                // so it goes straight to stderr rather than through
                                // the fixed-size log buffer that would truncate it.
                                std::fprintf(stderr, "%s:\n%s\n", msg,
                                             leaked != nullptr ? leaked : "?");
                            }
                        }
                    }
                }
                return 1;
            });

                    if (native_activity_create) {
                        appendAndEcho("[kudroid_core] Native Game Activity detected.");

                        appendAndEcho("[kudroid_core] Found ANativeActivity_onCreate, invoking...");

                        static std::string s_internalDataPath;
                        static std::string s_externalDataPath;
                        static std::string s_obbPath;
                        s_internalDataPath = "/data/data/" + resolvedAppName;
                        s_externalDataPath = "/sdcard/Android/data/" + resolvedAppName;
                        s_obbPath = "/sdcard/Android/obb/" + resolvedAppName;

                        std::error_code vfsEc;
                        std::filesystem::create_directories(std::filesystem::path(remapper.androidRoot()) / "data/data" / resolvedAppName, vfsEc);
                        std::filesystem::create_directories(std::filesystem::path(remapper.androidRoot()) / "sdcard/Android/data" / resolvedAppName, vfsEc);
                        std::filesystem::create_directories(std::filesystem::path(remapper.androidRoot()) / "sdcard/Android/obb" / resolvedAppName, vfsEc);

                        static ANativeActivityCallbacks mock_callbacks = {};
                        static ANativeActivity mock_activity = {
                            &mock_callbacks,
                            jvm,
                            nullptr, // env
                            nullptr, // clazz
                            s_internalDataPath.c_str(),
                            s_externalDataPath.c_str(),
                            29, // sdkVersion (Android 10)
                            nullptr, // instance
                            nullptr, // assetManager
                            s_obbPath.c_str()
                        };
                        kuart_get_env(jvm, reinterpret_cast<void**>(&mock_activity.env), 0);

                        if (mock_activity.env) {
                            JNIEnv* env = mock_activity.env;
                            if (mock_activity.assetManager) {
                                env->DeleteGlobalRef(static_cast<jobject>(mock_activity.assetManager));
                                mock_activity.assetManager = nullptr;
                            }
                            jclass assetCls = env->FindClass("android/content/res/AssetManager");
                            if (assetCls && !env->ExceptionCheck()) {
                                jmethodID ctor = env->GetMethodID(assetCls, "<init>", "()V");
                                jobject am = ctor ? env->NewObject(assetCls, ctor) : nullptr;
                                if (am) {
                                    mock_activity.assetManager = env->NewGlobalRef(am);
                                    env->DeleteLocalRef(am);
                                    appendAndEcho("[kudroid_core] AssetManager jobject created.");
                                }
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                env->DeleteLocalRef(assetCls);
                            } else {
                                if (env->ExceptionCheck()) env->ExceptionClear();
                            }
                        }

                        native_activity_create(&mock_activity, nullptr, 0);
                        appendAndEcho("[kudroid_core] ANativeActivity_onCreate completed.");

                        auto call1 = [&](const char* name, void* fn) {
                            if (!fn) return;
                            appendAndEcho(std::string("[kudroid_core] Calling ") + name);
                            reinterpret_cast<void (*)(ANativeActivity*)>(fn)(&mock_activity);
                        };
                        auto call2 = [&](const char* name, void* fn, void* arg2) {
                            if (!fn) return;
                            appendAndEcho(std::string("[kudroid_core] Calling ") + name);
                            reinterpret_cast<void (*)(ANativeActivity*, void*)>(fn)(&mock_activity, arg2);
                        };
                        call1("onStart", mock_callbacks.onStart);
                        call1("onResume", mock_callbacks.onResume);
                        call2("onWindowFocusChanged", mock_callbacks.onWindowFocusChanged, reinterpret_cast<void*>(1));
                        call2("onNativeWindowCreated", mock_callbacks.onNativeWindowCreated, bionic_ANativeWindow_fromSurface(nullptr, nullptr));
                        call2("onInputQueueCreated", mock_callbacks.onInputQueueCreated, kudroid_get_input_queue());
                        appendAndEcho("[kudroid_core] Lifecycle callbacks invoked successfully!");
                    } else {
                        appendAndEcho("[kudroid_core] Java APK Application detected (No ANativeActivity_onCreate).");
                        
                        std::string targetActivity = "";
                        std::string pkgName = "";
                        // Everything the manifest declares. Populated below and used
                        // as the fallback list, so KuDroid only ever tries classes the
                        // app actually declares.
                        std::vector<std::string> manifestActivities;
                        std::string manifestAppClass;
                        // android:appComponentFactory. Android instantiates it before
                        // any component, so its <clinit> is the first guest code to
                        // run; skipping it leaves whatever the app initialises there
                        // empty. Generic: whatever the manifest names, nothing assumed.
                        std::string manifestComponentFactory;

                        // ƯU TIÊN 1: parse AndroidManifest.xml ĐÃ GIẢI NÉN trong
                        // appDir — nguồn chính xác duy nhất. Log cũ cho thấy
                        // app_info.json có thể thiếu/stale (Target Activity bị
                        // đoán "Minecraft.MainActivity" trong khi package thật
                        // là com.mojang.minecraftpe) → ClassNotFoundException.
                        const auto manifestPath = appDir / "AndroidManifest.xml";
                        if (std::filesystem::exists(manifestPath)) {
                            std::ifstream mf(manifestPath, std::ios::binary);
                            if (mf) {
                                std::vector<std::uint8_t> axml(
                                    (std::istreambuf_iterator<char>(mf)),
                                    std::istreambuf_iterator<char>());
                                appendAndEcho("[kudroid_core] Parsing AndroidManifest.xml (" +
                                              std::to_string(axml.size()) + " bytes)...");
                                kudroid::ManifestInfo mi =
                                    kudroid::APKExtractor::parse_manifest(axml.data(), axml.size());
                                // APK repack (apktool/BANDISHARE...) thường chứa
                                // manifest dạng TEXT — AXML parser trả rỗng với
                                // chúng. Thử parse text trước khi bỏ cuộc.
                                if (mi.mainActivity.empty() && axml.size() > 4 &&
                                    axml[0] == '<') {
                                    appendAndEcho("[kudroid_core] Manifest is TEXT XML, trying text parser...");
                                    mi = kudroid::APKExtractor::parse_manifest_text(
                                        reinterpret_cast<const char*>(axml.data()), axml.size());
                                }
                                appendAndEcho("[kudroid_core] Manifest parse: package='" + mi.packageName +
                                              "' mainActivity='" + mi.mainActivity + "'" +
                                              " activities=" + std::to_string(mi.activities.size()) +
                                              (mi.appClass.empty() ? "" : " application='" + mi.appClass + "'") +
                                              (mi.appComponentFactory.empty()
                                                   ? ""
                                                   : " appComponentFactory='" + mi.appComponentFactory + "'"));
                                if (!mi.mainActivity.empty()) targetActivity = mi.mainActivity;
                                if (!mi.packageName.empty()) pkgName = mi.packageName;
                                manifestAppClass = mi.appClass;
                                manifestComponentFactory = mi.appComponentFactory;
                                // The manifest is the authoritative list of what this
                                // app can launch: launcher activities first, then the
                                // rest. Feeding these to ActivityThread replaces the
                                // old habit of inventing names like "<pkg>.Main",
                                // which could never exist unless the app happened to
                                // use that exact name.
                                manifestActivities = mi.launchOrder();
                                for (const auto& a : mi.activities) {
                                    appendAndEcho(std::string("[kudroid_core]   manifest activity: ") + a.name +
                                                  (a.isLauncher ? "  [LAUNCHER]" : "") +
                                                  (a.isAlias ? "  [alias]" : ""));
                                }
                            } else {
                                appendAndEcho("[kudroid_core] WARNING: Cannot open AndroidManifest.xml");
                            }
                        } else {
                            appendAndEcho("[kudroid_core] WARNING: AndroidManifest.xml not found in " + appDir.string());
                        }

                        // ƯU TIÊN 2: app_info.json do extractor ghi lúc cài đặt.
                        if (targetActivity.empty()) {
                            std::filesystem::path infoPath = appDir / "app_info.json";
                            if (std::filesystem::exists(infoPath)) {
                                std::ifstream f(infoPath);
                                std::string line;
                                while (std::getline(f, line)) {
                                    auto posAct = line.find("\"main_activity\": \"");
                                    if (posAct != std::string::npos) {
                                        auto start = posAct + 18;
                                        auto end = line.find("\"", start);
                                        if (end != std::string::npos) {
                                            std::string act = line.substr(start, end - start);
                                            if (!act.empty()) targetActivity = act;
                                        }
                                    }
                                    auto posPkg = line.find("\"package\": \"");
                                    if (posPkg != std::string::npos) {
                                        auto start = posPkg + 12;
                                        auto end = line.find("\"", start);
                                        if (end != std::string::npos) {
                                            pkgName = line.substr(start, end - start);
                                        }
                                    }
                                }
                            }
                        }

                        // ƯU TIÊN 3: quét class trong DEX của app tìm Activity.
                        // KuART đọc thẳng classes*.dex nên không cần classes.jar.
                        // Danh sách Activity thật verify được — dùng làm fallback
                        // cho ActivityThread, khai báo ở scope này để dùng được cả
                        // khi khối quét không chạy.
                        std::vector<std::string> verifiedActivities;
                        if (targetActivity.empty()) {
                            std::vector<std::string> classes;
                            {
                                constexpr size_t kMaxClasses = 20000;
                                std::vector<char*> raw(kMaxClasses, nullptr);
                                const size_t n = kuart_list_app_classes(raw.data(), kMaxClasses);
                                classes.reserve(n);
                                for (size_t i = 0; i < n; ++i) classes.emplace_back(raw[i]);
                                kuart_free_class_list(raw.data(), n);
                            }
                            {
                                appendAndEcho("[kudroid_core] Scanning app DEX for Activity classes...");
                                appendAndEcho("[kudroid_core] Found " + std::to_string(classes.size()) +
                                              " non-system classes in app DEX");
                                // Chấm điểm chọn launcher:
                                // -1000  Activity của SDK bên thứ ba (push/analytics
                                // v.v.) — KHÔNG phải entry point, launch nó
                                // chỉ cho màn hình xám (bài học Braze).
                                // +100   tên chứa "Activity"
                                // +50    package khớp pkgName (từ manifest/app_info)
                                // +depth bonus: package càng ngắn (gốc app) càng
                                // giống entry point chính.
                                static const char* kSdkActivityHints[] = {
                                    "braze", "facebook", "firebase", "google", "admob",
                                    "unity3d", "appsflyer", "adjust", "amplitude",
                                    "mixpanel", "crashlytics", "playfab", "microsoft",
"zarchiver" /* handled riêng ở trên */
                                };
                                // Tính sẵn MỘT LẦN thay vì trong vòng lặp từng
                                // class (jar lớn có thể có 10k+ class).
                                std::string pkgPrefix;
                                if (!pkgName.empty()) {
                                    // pkgName dạng a.b.c → prefix "a/b/c/".
                                    pkgPrefix = pkgName;
                                    for (char& c : pkgPrefix) if (c == '.') c = '/';
                                    pkgPrefix += '/';
                                }
                                auto isSdkOwned = [](const std::string& cls) {
                                    for (const char* hint : kSdkActivityHints) {
                                        if (cls.find(hint) != std::string::npos) return true;
                                    }
                                    return false;
                                };
                                std::string best;
                                int bestScore = -1;
                                std::string bestAny; // fallback nếu không có *Activity nào
                                int bestAnyScore = -1;
                                for (const auto& cls : classes) {
                                    const bool isActivity = cls.find("Activity") != std::string::npos;
                                    const size_t depth = static_cast<size_t>(
                                        std::count(cls.begin(), cls.end(), '/'));
                                    int score = (isActivity ? 100 : 0) + static_cast<int>(10 - depth);
                                    // Launcher activity hầu như luôn tên "...MainActivity".
                                    if (cls.size() >= 13 &&
                                        cls.compare(cls.size() - 13, 13, "/MainActivity") == 0) score += 80;
                                    if (!pkgPrefix.empty() &&
                                        cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) score += 50;
                                    if (isSdkOwned(cls)) score -= 1000;

                                    if (isActivity && score > bestScore) {
                                        bestScore = score;
                                        best = cls;
                                    }
                                    if (score > bestAnyScore) {
                                        bestAnyScore = score;
                                        bestAny = cls;
                                    }
                                }
                                if (!best.empty() && bestScore > 0) {
                                    for (char& c : best) if (c == '/') c = '.';
                                    targetActivity = best;
                                    appendAndEcho("[kudroid_core] Resolved from app DEX: " + best);
                                } else if (!bestAny.empty() && bestAnyScore > 0) {
                                    // Không có *Activity "sạch" — dùng class điểm cao nhất.
                                    for (char& c : bestAny) if (c == '/') c = '.';
                                    targetActivity = bestAny;
                                    appendAndEcho("[kudroid_core] No clean *Activity class; using highest-scored app class: " + bestAny);
                                } else if (!classes.empty()) {
                                    // Mọi class đều bị trừ điểm SDK — log vài cái để debug.
                                    for (size_t i = 0; i < classes.size() && i < 5; ++i) {
                                        appendAndEcho("[kudroid_core]   candidate(all-SDK?): " + classes[i]);
                                    }
                                    std::string first = classes.front();
                                    for (char& c : first) if (c == '/') c = '.';
                                    targetActivity = first;
                                    appendAndEcho("[kudroid_core] Using first app class as last resort: " + first);
                                }

                                // ƯU TIÊN 3.5 — VERIFY: tên class KHÔNG nói lên
                                // gì khi app bị ProGuard obfuscate (a.a.a v.v.).
                                // Kiểm tra candidate THẬT SỰ extends
                                // android.app.Activity qua chủng loại của KuART.
                                //
                                // BÀI HỌC Braze: vòng verify KHÔNG được lấy Activity
                                // ĐẦU TIÊN tìm thấy — SDK Activity (push/analytics)
                                // cũng extends Activity nhưng không render gì → màn
                                // hình xám. Dùng lại kSdkActivityHints + isSdkOwned
                                // đã khai báo ở trên (chấm điểm rồi chọn cao nhất).

                                if (!targetActivity.empty() &&
                                    kuart_class_extends_activity(targetActivity.c_str()) != 1) {
                                    appendAndEcho("[kudroid_core] Candidate '" + targetActivity +
                                                  "' does NOT extend Activity (obfuscated?). Verifying all classes...");
                                    targetActivity.clear();
                                    int verifiedBestScore = -1;
                                    std::string verifiedBest;
                                    int checked = 0;
                                    // Thu thập TẤT CẢ Activity thật tìm được (không
                                    // chỉ cái tốt nhất) — truyền xuống làm fallback
                                    // cho ActivityThread khi candidate chính fail.
                                    // FindClass phải link cả chuỗi kế thừa nên tốn
                                    // kém; với DEX 10k+ class KHÔNG quét toàn bộ.
                                    // Pass 0: chỉ class có tên chứa "Activity" (phủ
                                    // đại đa số app). Pass 1 (chỉ chạy khi pass 0
                                    // không thấy gì — app obfuscate hoàn toàn): phần
                                    // còn lại, giới hạn 2000 lần kiểm.
                                    for (int pass = 0; pass < 2 && verifiedBestScore <= 0; ++pass) {
                                        int checkedThisPass = 0;
                                        for (const auto& cls : classes) {
                                            const bool hasName = cls.find("Activity") != std::string::npos;
                                            if (pass == 0 && !hasName) continue;
                                            if (pass == 1 && hasName) continue;
                                            if (++checkedThisPass > (pass == 0 ? 8000 : 2000)) break;
                                            std::string dotted = cls;
                                            for (char& c : dotted) if (c == '/') c = '.';
                                            ++checked;
                                            if (kuart_class_extends_activity(dotted.c_str()) != 1) continue;
                                            // Là Activity thật — chấm điểm như trên.
                                            const size_t depth = static_cast<size_t>(
                                                std::count(cls.begin(), cls.end(), '/'));
                                            int score = 100 + static_cast<int>(10 - depth);
                                            if (cls.size() >= 13 &&
                                                cls.compare(cls.size() - 13, 13, "/MainActivity") == 0) score += 80;
                                            if (!pkgPrefix.empty() &&
                                                cls.compare(0, pkgPrefix.size(), pkgPrefix) == 0) score += 50;
                                            if (isSdkOwned(cls)) score -= 1000;
                                            appendAndEcho("[kudroid_core]   Activity candidate: " + dotted +
                                                          " (score=" + std::to_string(score) + ")");
                                            verifiedActivities.push_back(dotted);
                                            if (score > verifiedBestScore) {
                                                verifiedBestScore = score;
                                                verifiedBest = dotted;
                                            }
                                            if (verifiedBestScore >= 185) break; // pkg match + MainActivity — đủ chắc
                                        }
                                    }
                                    appendAndEcho("[kudroid_core] Activity verify done: " +
                                                  std::to_string(checked) + " classes checked");
                                    if (!verifiedBest.empty() && verifiedBestScore > 0) {
                                        targetActivity = verifiedBest;
                                        appendAndEcho("[kudroid_core] Verified best Activity: " + verifiedBest);
                                    } else if (!verifiedBest.empty()) {
                                        // Mọi Activity đều thuộc SDK — chọn ít tệ nhất nhưng cảnh báo rõ.
                                        targetActivity = verifiedBest;
                                        appendAndEcho("[kudroid_core] WARNING: only SDK Activities found; using least-bad: " + verifiedBest);
                                    } else {
                                        appendAndEcho("[kudroid_core] WARNING: no class in app DEX extends android.app.Activity");
                                    }
                                } else if (!targetActivity.empty()) {
                                    appendAndEcho("[kudroid_core] Verified: '" + targetActivity + "' extends Activity ✓");
                                }
                            }
                        }

                        // ƯU TIÊN 4: đoán theo package/tên app — CHỈ khi cả ba
                        // nguồn trên thất bại. Hoàn toàn tổng quát: không hardcode
                        // app cụ thể nào; các biến thể tên được ActivityThread
                        // thử động từ package prefix (xem ActivityThread.java).
                        std::string guessBase; // dùng chung cho fallback list
                        // Prefer any manifest-declared activity over a guess: the
                        // manifest is what Android itself reads.
                        if (targetActivity.empty() && !manifestActivities.empty()) {
                            targetActivity = manifestActivities.front();
                            appendAndEcho("[kudroid_core] Using first manifest-declared activity: " +
                                          targetActivity);
                        }
                        if (targetActivity.empty()) {
                            if (!pkgName.empty()) {
                                targetActivity = pkgName + ".MainActivity";
                            } else {
                                std::string base = appName;
                                auto uIdx = base.find('_');
                                if (uIdx != std::string::npos) {
                                    base = base.substr(0, uIdx);
                                }
                                targetActivity = base + ".MainActivity";
                            }
                            appendAndEcho("[kudroid_core] WARNING: guessed Activity '" + targetActivity +
"' (manifest/jar-scan/JNI đều thất bại)");
                        }

                        // Fallback list for ActivityThread, in descending order of
                        // authority:
                        //   1. activities the MANIFEST declares (launcher first),
                        //   2. classes verified to extend android.app.Activity.
                        //
                        // Names are no longer invented from the package. Guessed
                        // candidates like "<pkg>.Main" only ever produced
                        // ClassNotFoundException — an app either declares an activity
                        // in its manifest or it cannot be launched, which is exactly
                        // how Android decides. The only guess left is the last-resort
                        // targetActivity above, used when there is no manifest at all.
                        std::vector<std::string> fallbackStorage;
                        auto addFallback = [&](const std::string& s) {
                            if (s.empty() || s == targetActivity) return;
                            for (const auto& f : fallbackStorage)
                                if (f == s) return;
                            if (fallbackStorage.size() < 12) fallbackStorage.push_back(s);
                        };
                        for (const auto& a : manifestActivities) addFallback(a);
                        for (const auto& v : verifiedActivities) addFallback(v);
                        std::vector<const char*> fallbackPtrs;
                        for (const auto& f : fallbackStorage) fallbackPtrs.push_back(f.c_str());

                        appendAndEcho("[kudroid_core] Target Activity: " + targetActivity);
                        if (!fallbackPtrs.empty()) {
                            appendAndEcho("[kudroid_core] Fallback candidates: " +
                                          std::to_string(fallbackPtrs.size()));
                        }
                        if (!manifestComponentFactory.empty()) {
                            appendAndEcho("[kudroid_core] appComponentFactory: " +
                                          manifestComponentFactory);
                        }
                        if (!manifestAppClass.empty()) {
                            appendAndEcho("[kudroid_core] Application class: " + manifestAppClass);
                        }
                        appendAndEcho("[kudroid_core] Launching Android ActivityThread runtime (KuART)...");
                        if (!kuart_launch_app(pkgName.c_str(),
                                              manifestComponentFactory.c_str(),
                                              manifestAppClass.c_str(),
                                              targetActivity.c_str(),
                                              fallbackPtrs.empty() ? nullptr : fallbackPtrs.data(),
                                              static_cast<int>(fallbackPtrs.size()))) {
                            appendAndEcho("[kudroid_core] ERROR: ActivityThread.main failed: " +
                                          std::string(kuart_last_error()));
                        }
                    }
                }
            }
        }
    }

    const char* trace = kudroid::bionic_shim_trace();
    if (trace && *trace) {
        log += "[kudroid_core] Bionic/global binding trace:\n";
        log += trace;
    }

    writeLogFile("kudroid_run_apk.txt", log);
    s_isApkRunning.store(false);
    char* result = static_cast<char*>(malloc(log.size() + 1));
    if (result) memcpy(result, log.c_str(), log.size() + 1);
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

            // thử ánh xạ (hiện tại là hàm giả)
            log += "[kudroid_core] Phase: map PT_LOAD segments\n";
            if (loader.map()) {
                log += "[kudroid_core] Map OK.\n";
            } else {
                snprintf(buf, sizeof(buf), "[kudroid_core] Map failed: %s\n", loader.lastError());
                log += buf;
            }

            // thử định vị lại (hiện tại là hàm giả)
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

    // từ chối chạy mã gốc khi jit bị tắt: việc thực thi các trang prot_exec
    // mà không có ký mã động sẽ làm lỗi toàn bộ quá trình và nhật ký
    // được đệm bên dưới sẽ không bao giờ được trả về. hãy thất bại lớn tiếng thay vì gặp sự cố.
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

        // chụp nhanh nhật ký cho trình xử lý sự cố: lời gọi bên dưới nhảy vào
        // mã đã được jit và có thể bị lỗi (tín hiệu, không phải ngoại lệ). nếu có,
        // trình xử lý sẽ đẩy bộ đệm này vào kudroid_crash.log.
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

    // Tìm entrypoint
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

    // Hỗ trợ chữ ký hàm 64-bit trả về const char* hoặc uintptr_t
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
    
    // xây dựng đường dẫn đến thư mục bộ đệm của ứng dụng bằng logic ánh xạ vfs
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

// Xóa app với báo cáo tiến trình qua callback (phase UTF-8, percent 0-100).
// Callback chạy trên thread gọi hàm — Swift side tự dispatch về main thread.
// Cho phép UI hiển thị progress thay vì freeze vài giây khi remove_all quét
// hàng nghìn file (assets, DEX, .so...).
typedef void (*kudroid_delete_progress_cb)(const char* phase, int percent, void* userdata);

namespace {
// ── [DEBUG UNINSTALL] ghi vết từng bước xóa app ──────────────────────────
// Triệu chứng: xóa app trên máy thật kẹt ở 0% không có dấu hiệu gì. Bộ ghi
// vết này lưu TOÀN BỘ diễn biến (đường dẫn, quyền ghi, số file xóa được/
// thất bại, lỗi std::filesystem, số lần callback...) vào
// <Documents>/kudroid_uninstall_debug.txt và trả về Swift qua
// kudroid_uninstall_debug_log() để xem/copy ngay trong tab Debug.
// File được ghi LẠI SAU MỖI dòng — kể cả khi tiến trình chết giữa chừng,
// log vẫn còn đến đúng bước cuối nó đạt được.
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
            ::chmod(path.c_str(), 0644); // AFC đọc được như stderr.log
        }
    }
}

// Mô tả ngắn trạng thái 1 đường dẫn: tồn tại? loại? ghi được? — bắt lỗi
// quyền NGAY TỪ ĐẦU thay vì đoán mò vì sao remove thất bại im lặng.
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

// Xóa cây NHANH theo con cấp 1: mỗi thư mục con được remove_all nguyên khối.
// Unlink là thao tác metadata — tốc độ gần như KHÔNG phụ thuộc dung lượng,
// chỉ phụ thuộc số entry — đây là lý do bản cũ "freeze vài giây" là xong.
// Bản thử progress bằng cách xóa từng file sâu nhất đã đi bộ hàng nghìn
// entry qua std::filesystem → MCPE 932MB chậm như treo ở 0%. Ở đây:
// - Không đếm bytes toàn cây (trước đây tốn >3s chỉ để tính %).
// - Progress nhảy theo từng con cấp 1 (lib/, assets/, res/...) — đủ mượt.
// - Log thời gian từng con vào trace; con nào ≥2s bị đánh dấu SLOW.
void removeTreeWithProgress(const std::filesystem::path& p,
                            const char* phase,
                            kudroid_delete_progress_cb cb, void* ud,
                            double basePct, double spanPct,
std::uint64_t /*totalBytes - không dùng nữa*/) {
    const auto nowMs = [] {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    };
    std::error_code ec;
    uninstallDbg("phase='" + std::string(phase) + "' path=" + describePath(p));
    if (!std::filesystem::exists(p, ec)) return;

    // File đơn (hiếm).
    if (!std::filesystem::is_directory(p, ec)) {
        const std::int64_t t0 = nowMs();
        std::filesystem::remove(p, ec);
        uninstallDbg("  single-file removed in " + std::to_string(nowMs() - t0) +
                     "ms ec=" + (ec ? ec.message() : std::string("none")));
        return;
    }

    // Liệt kê con cấp 1 (không đệ quy) — rẻ, chỉ đọc entry thư mục.
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
    // Xóa nốt thư mục gốc (giờ đã rỗng).
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
    const std::filesystem::path dalvikRoot =
        std::filesystem::path(androidRoot) / "data/dalvik-cache";
    uninstallDbg("code:   " + describePath(appCodePath));
    uninstallDbg("data:   " + describePath(appDataPath));
    uninstallDbg("dalvik: " + describePath(dalvikRoot));

    // KHÔNG đếm bytes toàn cây nữa — với MCPE 932MB việc đi bộ hết cây mất
    // >3s trên máy thật chỉ để tính %. Progress giờ chia theo con cấp 1.
    std::error_code ec;
    // dalvik-cache là tàn dư của đường dex2jar cũ; vẫn xóa để cài lại không để
    // lại artifact mục ruỗng. Khớp cả "<pkg>" lẫn "<tên-file-apk>_<ver>".
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
    // Chia %: code 0-45, data 45-70, dalvik 70-100.
    removeTreeWithProgress(appCodePath, "Removing app files",
                           cb, userdata, 0.0, 45.0, 0);
    removeTreeWithProgress(appDataPath, "Removing app data",
                           cb, userdata, 45.0, 25.0, 0);

    if (cb) cb("Removing compiled cache", 70, userdata);
    for (const auto& m : dalvikMatches) {
        removeTreeWithProgress(m, "Removing compiled cache",
                               cb, userdata, 70.0, 30.0, 0);
    }
    if (cb) cb("Done", 100, userdata);
    uninstallDbg("=== UNINSTALL DONE success=" + std::to_string(success) + " ===");
    return success;
}

extern "C" int kudroid_delete_app(const char* package_name) {
    return kudroid_delete_app_progress(package_name, nullptr, nullptr);
}

// Nhật ký debug chi tiết của lần uninstall gần nhất — Swift hiển thị/copy
// ở tab Debug. Chuỗi được malloc; caller phải free().
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
                
                // xóa bộ đệm theo dõi bionicshim
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
            
            // thực thi jni_onload nếu có
            void* jniOnLoadAddr = loader.getSymbolAddress("JNI_OnLoad");
            if (jniOnLoadAddr) {
                log += "[kudroid_jni] Found JNI_OnLoad, executing...\n";
                mirrorCrash(log);
                // lấy jvm từ bộ nối
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
                
                // thiết lập lệnh gọi lại
                static std::string* g_jni_test_log = &log;
                g_jni_test_log = &log;
                kuart_set_log_callback([](const char* msg) {
                    if (g_jni_test_log) {
                        *g_jni_test_log += "[KuART] ";
                        *g_jni_test_log += msg;
                        *g_jni_test_log += "\n";
                    }
                });

                kuart_init(""); // framework nhúng là đủ cho test này
                mirrorCrash(log);
                void* vm = kuart_get_javavm();
                
                int result = test_func(vm);
                log += "[kudroid_jni] TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
                mirrorCrash(log);
                log += "[kudroid_jni] TEST RESULT: " +
                       (result == 0 ? std::string("0 (SUCCESS)") : std::to_string(result) + " (FAILED)") + "\n";
            }
        }
    }
    writeLogFile("kudroid_jni_massive_test.txt", log);
    return strdup(log.c_str());
}

// các bài kiểm tra thực thi gpu .so — tải tệp .so kiểm tra elf arm64 thông qua trình
// tải elf, trình này gọi dlopen/dlsym cho các thư viện gpu. bionicshim chặn các lời gọi đó
// và ánh xạ chúng trực tiếp tới gốc ios (moltenvk / angle).
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
    // Dùng biến cục bộ + strdup (không phải static): hàm trước đó trả con trỏ vào
    // static std::string bị clear ở lần gọi sau — không thread-safe và dễ sai.
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

    // Cho AAssetManager shim biết nơi chứa assets đã extract của APK này.
    kudroid_set_assets_dir((targetDir + "/assets").c_str());
    
    log += "[kudroid_apk] Scanning for native libraries (.so)...\n";
    // Process-lifetime (xem globalLibraryManager) — JNI_OnLoad dưới đây spawn
    // render thread guest; mappings phải ở lại sau khi hàm này return.
    kudroid::LibraryManager& libManager = globalLibraryManager();
    std::string libDir = targetDir + "/lib/arm64-v8a";

    // ── KuART ──────────────────────────────────────────────────────────────
    // Nạp framework.dex nhúng + classes*.dex của APK. Không còn bước dịch
    // DEX→JAR nào: KuART thông dịch DEX trực tiếp.
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

// ─────────────────────────────────────────────────────────────────────────────
// Kiểm tra nạp DEX bằng KuART
//
// Trước đây hàm này dịch DEX → JAR cho Avian. KuART nạp DEX trực tiếp nên giờ nó
// chỉ parse và báo cáo nội dung — giữ tên cũ để vỏ Swift không phải đổi.
// Trả chuỗi log đã malloc; caller phải free().
// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_translate_dex(const char* dexPath) {
    if (!dexPath || !*dexPath) {
        return strdup("[kudroid_dex] ERROR: null DEX path\n");
    }

    std::string log;
    log += "[kudroid_dex] Loading DEX with KuART: " + std::string(dexPath) + "\n";

    // Nạp thư mục chứa file DEX — KuART lấy mọi classes*.dex trong đó.
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

// ─────────────────────────────────────────────────────────────────────────────
// Gentle Crash API
// ─────────────────────────────────────────────────────────────────────────────
extern "C" int kudroid_has_crashed(void) {
    return g_hasCrashed.load() ? 1 : 0;
}

extern "C" void kudroid_clear_crash_state(void) {
    g_hasCrashed.store(false);
    g_lastCrashTail[0] = '\0';
}

extern "C" const char* kudroid_get_last_crash_tail(void) {
    if (g_lastCrashTail[0] == '\0') {
        return strdup("No recent crash detected.");
    }
    return strdup(g_lastCrashTail);
}

// ─────────────────────────────────────────────────────────────────────────────
// Permission Dialog Prompt Bridge
// ─────────────────────────────────────────────────────────────────────────────
static kudroid_permission_prompt_cb g_permission_prompt_cb = nullptr;

extern "C" void kudroid_set_permission_prompt_callback(kudroid_permission_prompt_cb cb) {
    g_permission_prompt_cb = cb;
}

extern "C" void kudroid_prompt_permission_request(const char* packageName, const char* permissionsCsv, int requestCode, void* activityHandle) {
    if (g_permission_prompt_cb != nullptr) {
        g_permission_prompt_cb(packageName, permissionsCsv, requestCode, activityHandle);
    } else {
        kudroid_submit_permission_response(activityHandle, requestCode, permissionsCsv, 1);
    }
}

extern "C" void kudroid_submit_permission_response(void* activityHandle, int requestCode, const char* permissionsCsv, int granted) {
    if (activityHandle == nullptr || permissionsCsv == nullptr) return;
    std::string csv = permissionsCsv;
    std::vector<std::string> perms;
    std::string token;
    std::istringstream tokenStream(csv);
    while (std::getline(tokenStream, token, ',')) {
        if (!token.empty()) perms.push_back(token);
    }
    if (perms.empty()) return;

    // Log permission grant result
    std::fprintf(stderr, "[PermissionManager] Permission response for request %d (%zu perms): %s\n",
                 requestCode, perms.size(), granted ? "GRANTED" : "DENIED");
}