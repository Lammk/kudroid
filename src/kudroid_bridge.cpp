#include "kudroid/elf_loader.hpp"
#include "kudroid/BionicShim.h"
#include "kudroid/VFSPathRemapper.h"
#include "kudroid/APKExtractor.h"
#include "kudroid/DexCacheManager.h"
#include "kudroid/DexAotCache.h"
#include "kudroid/platform/InputShim.h"
#include "kudroid/platform/AssetShim.h"
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
#include <filesystem>
#include <set>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ucontext.h>
#include <dlfcn.h>
#include <unwind.h>
#include <mutex>

extern "C" struct JavaVM_* kudroid_jni_get_javavm(void);

// ── ghi nhật ký liên tục vào thư mục có thể ghi của ứng dụng ─────────────────────────
// ứng dụng truyền một thư mục (thư mục documents của nó) thông qua kudroid_set_log_dir().
// nhật ký thành công được ghi dưới dạng tệp .txt; các sự cố (dựa trên tín hiệu, do đó không có ngoại lệ
// c++ nào được kích hoạt) được trình xử lý tín hiệu bắt, bộ đệm nhật ký được đẩy
// vào đĩa chỉ bằng các lệnh gọi an toàn tín hiệu bất đồng bộ trước khi kích hoạt lại.
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
static int kudroid_jit_available(void);

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
//  1. ObjC exception chưa bắt (NSException — ANGLE/Metal/UIKit hay ném) → reason
//  2. C++ exception chưa bắt (std::terminate) → what()
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
        "kudroid_core v0.1.5 " __DATE__ " " __TIME__ " "
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

static void crashHandler(int sig, siginfo_t* info, void* ucontext) {
    if (sig == SIGTRAP) {
        if (kudroid::bionic_handle_tpidr_trap(ucontext)) {
            return; // đã xử lý thành công, tiếp tục thực thi!
        }
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
                        const bool guardLike =
                            slot56 > 0x100000000ULL && slot56 < 0x7fffffffffffULL;
                        int n2 = snprintf(sigline, sizeof(sigline),
                            "fp%02d: f=0x%llx lr=0x%llx%s slot56=0x%llx%s\n",
                            i, (unsigned long long)f, (unsigned long long)savedLr,
                            inGuest ? lrMod : "?", (unsigned long long)slot56,
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
    signal(sig, SIG_DFL);
    raise(sig);
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
    strncpy(g_logDir, dir, sizeof(g_logDir) - 1);
    g_logDir[sizeof(g_logDir) - 1] = '\0';
    installCrashHandlers();

    // Ghi stamp build ra file để user kiểm tra app đang chạy có phải bản mới
    // nhất không (trả lời "iPhone vẫn chạy app cũ?").
    const char* stamp = kudroid_build_stamp();
    writeLogFile("kudroid_version.txt", std::string(stamp) + "\n");

#if defined(__APPLE__)
    // Redirect stderr (fd 2) vào file — Avian (JVM nhúng) in lý do abort/fatal
    // ra stderr (vd expect()/abort(c) trong compiler kèm op không hỗ trợ) nhưng
    // iOS không hiển thị stderr đâu cả → lý do crash trước đây bị mất hoàn toàn.
    // Crash handler sẽ dump đuôi file này vào kudroid_crash.log.
    {
        char errPath[1200];
        size_t dl = strlen(g_logDir);
        if (dl < sizeof(errPath) - 32) {
            memcpy(errPath, g_logDir, dl);
            memcpy(errPath + dl, "/stderr.log", 12);
            int errFd = open(errPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (errFd >= 0) {
                dup2(errFd, STDERR_FILENO);
                close(errFd);
            }
        }
    }
#endif
}

extern "C" void kudroid_set_documents_dir(const char* dir) {
    if (dir) kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(dir);
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

    // ANGLE first-touch phải trên main thread (GL TEST chứng minh OK; triangle
    // first-touch trên render pthread -> abort trong static init ANGLE).
    kudroid_gpu_warmup_egl();
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
        std::string appName = source.stem().string();
        if (appName.empty()) appName = "unnamed_apk";
        for (char& character : appName) {
            if (!(std::isalnum(static_cast<unsigned char>(character)) || character == '_' || character == '-')) {
                character = '_';
            }
        }
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path target = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / appName;
        log += "[kudroid_apk] APK: " + source.string() + "\n";
        log += "[kudroid_apk] Target extraction directory: " + target.string() + "\n";
        bool extractedOk = false;
        if (kudroid::APKExtractor::is_bundle_container(source.string())) {
            log += "[kudroid_apk] Split-APK bundle detected (.xapk/.apks/.apkm), merging splits...\n";
            extractedOk = kudroid::APKExtractor::extract_bundle(source.string(), target.string());
        } else {
            extractedOk = kudroid::APKExtractor::extract_apk(source.string(), target.string());
        }
        if (extractedOk) {
            log += "[kudroid_apk] APK extracted successfully\n";
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

// trả về 1 nếu jit (bộ nhớ có thể thực thi) có vẻ khả dụng, ngược lại trả về 0.
static int kudroid_jit_available(void) {
    unsigned int flags = 0;
    // cs_debugged là tín hiệu đáng tin cậy duy nhất trên ios: nó được đặt khi trình gỡ lỗi
    // (livecontainer/debugserver) đã bật ký mã động, điều này
    // chính xác cho phép thực thi các trang prot_exec. thăm dò rwx mmap không
    // đáng tin cậy — lời gọi hệ thống thành công mà không cần jit, nhưng quá trình thực thi vẫn bị lỗi,
    // đưa ra kết quả sai "enabled".
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

#include "kudroid/kudroid_jni.h"

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


// khai báo setter bên ngoài
extern "C" void kudroid_jni_set_log_callback(void (*cb)(const char*));

extern "C" char* kudroid_test_jvm(const char* rt_jar_path) {
    std::string log;
    appendTestHeader(log, "JVM Integration Test", "N/A");
    installCrashHandlers();
    
    log += "[kudroid_core] Phase: init JVM via JNI Bridge\n";
    kudroid::bionic_shim_reset_trace();

    // biến toàn cục để giữ tham chiếu nhật ký cho lệnh gọi lại c
    static std::string* g_jvm_test_log = &log;
    g_jvm_test_log = &log;
    
    kudroid_jni_set_log_callback([](const char* msg) {
        if (g_jvm_test_log) {
            *g_jvm_test_log += "[kudroid_jni] ";
            *g_jvm_test_log += msg;
            *g_jvm_test_log += "\n";
        }
    });

    kudroid_jni_init_jvm(rt_jar_path ? rt_jar_path : "", "");
    
    JavaVM* vm = kudroid_jni_get_javavm();
    if (!vm) {
        log += "[kudroid_core] ERROR: JavaVM is null!\n";
        return strdup(log.c_str());
    }
    
    JNIEnv* env = nullptr;
    kudroid_jni_get_env(vm, reinterpret_cast<void**>(&env), 0);
    
    if (env) {
        log += "[kudroid_core] Phase: testing JNI FindClass\n";
        jclass strClass = env->functions->FindClass(env, "java/lang/String");
        if (strClass) {
            log += "[kudroid_core] SUCCESS: Found java/lang/String class via JNI!\n";
        } else {
            log += "[kudroid_core] WARNING: java/lang/String class not found (expected if rt.jar is missing)\n";
        }
        
        jstring testStr = env->functions->NewStringUTF(env, "Hello JNI");
        if (testStr) {
            const char* utf = env->functions->GetStringUTFChars(env, testStr, nullptr);
            log += "[kudroid_core] SUCCESS: Created JNI string: ";
            log += utf ? utf : "null";
            log += "\n";
            env->functions->ReleaseStringUTFChars(env, testStr, utf);
        }
    } else {
        log += "[kudroid_core] ERROR: Failed to get JNIEnv!\n";
    }
    
    log += "[kudroid_core] Phase: destroy JVM\n";
    kudroid_jni_destroy_jvm();
    
    kudroid_jni_set_log_callback(nullptr);
    g_jvm_test_log = nullptr;

    log += "[kudroid_core] JVM test completed.\n";
    
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

extern "C" const char* kudroid_run_apk(const char* appName) {
    std::string log;
    appendTestHeader(log, "Run APK Native Libraries", appName);
    kudroid::bionic_shim_reset_trace();
    log += "[kudroid_core] Phase: init LibraryManager\n";

    if (!appName || !*appName) {
        log += "[kudroid_core] ERROR: null or empty app name\n";
    } else {
        auto& remapper = kudroid::VFSPathRemapper::getInstance();
        const std::filesystem::path libDir = std::filesystem::path(remapper.androidRoot()) /
                                             "data/app" / appName / "lib/arm64-v8a";
        // Cho AAssetManager shim biết nơi chứa assets đã extract (APKExtractor giữ
        // nguyên cây thư mục assets/ của APK). Game gọi AAssetManager_open với
        // đường dẫn tương đối — nếu thiếu, mọi truy cập asset trả NULL.
        const std::filesystem::path assetsDir = std::filesystem::path(remapper.androidRoot()) /
                                                "data/app" / appName / "assets";
        kudroid_set_assets_dir(assetsDir.string().c_str());
        
        log += "[kudroid_core] Scanning library directory: " + libDir.string() + "\n";
        
        if (!std::filesystem::exists(libDir)) {
            log += "[kudroid_core] ERROR: Library directory does not exist. Did you install the APK?\n";
        } else {
            // Process-lifetime (xem globalLibraryManager) — render thread guest
            // còn sống sau khi run_apk return, mappings phải ở lại.
            kudroid::LibraryManager& manager = globalLibraryManager();
            
            // thu thập tất cả các tệp .so để tải
            std::vector<std::string> soFiles;
            for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
                if (entry.path().extension() == ".so") {
                    soFiles.push_back(entry.path().string());
                }
            }
            
            if (soFiles.empty()) {
                log += "[kudroid_core] WARNING: No .so files found in the APK library directory.\n";
            } else {
                for (const auto& soPath : soFiles) {
                    log += "[kudroid_core] Attempting to load: " + soPath + "\n";
                    if (!manager.loadRecursive(soPath.c_str())) {
                        log += "[kudroid_core] LOAD FAILED for " + soPath + ": " + manager.lastError() + "\n";
                    } else {
                        log += "[kudroid_core] LOAD SUCCESS for " + soPath + "\n";
                    }
                }
                
                log += "[kudroid_core] Total loaded libraries (including dependencies): " + std::to_string(manager.libraries().size()) + "\n";
                log += "[kudroid_core] Native libraries loaded into memory successfully!\n";
                
                log += "[kudroid_core] --- Memory Map ---\n";
                for (const auto& pair : manager.libraries()) {
                    char mapLine[256];
                    snprintf(mapLine, sizeof(mapLine), "  %s -> %p\n", pair.first.c_str(), pair.second->baseAddress());
                    log += mapLine;
                }
                log += "[kudroid_core] ------------------\n";
                
                mirrorCrash(log);

                // ── PIPELINE DEX→JAR AOT CACHING ───────────────────────────
                // Avian không hiểu .dex: dịch toàn bộ classes*.dex của APK đã
                // extract thành classes.jar (qua d2j-dex2jar.sh, fallback bằng
                // trình dịch nhúng DexToJar), cache tại
                //   <android_root>/data/dalvik-cache/<appName>/
                // khóa bằng SHA-256 của classes*.dex (cache.hash). Jar cache
                // được đưa vào boot classpath của JVM lúc khởi tạo.
                const std::filesystem::path appDir =
                    std::filesystem::path(remapper.androidRoot()) / "data/app" / appName;
                const std::filesystem::path aotCacheDir =
                    std::filesystem::path(remapper.androidRoot()) /
                    "data/dalvik-cache" / appName;
                std::string aotError;
                const std::string classesJar = kudroid::DexAotCache::translate_dex_if_needed(
                    appDir.string(), aotCacheDir.string(), &aotError);

                // đảm bảo jvm avian được khởi tạo trước khi gọi jni_onload.
                // tệp boot jar (các lớp khung) được nhúng trong tệp nhị phân.
                if (!classesJar.empty()) {
                    log += "[kudroid_core] DEX→JAR AOT ready: " + classesJar + "\n";
                    mirrorCrash(log);
                    kudroid_jni_init_jvm("", classesJar.c_str());
                } else {
                    log += "[kudroid_core] WARNING: DEX→JAR AOT skipped (" +
                           aotError + "); JVM chạy không có classpath ứng dụng.\n";
                    mirrorCrash(log);
                    kudroid_jni_init_jvm("", "");
                }
                JavaVM* jvm = kudroid_jni_get_javavm();
                if (!jvm) {
                    log += "[kudroid_core] ERROR: Avian JVM failed to initialize. "
                           "JNI_OnLoad will not be invoked.\n";
                    mirrorCrash(log);
                } else {
                    char jvmLine[128];
                    snprintf(jvmLine, sizeof(jvmLine),
                             "[kudroid_core] Avian JVM ready (JavaVM=%p).\n", (void*)jvm);
                    log += jvmLine;

                    // Android gọi JNI_OnLoad cho TỪNG thư viện được loadLibrary.
                    // libraries_ là unordered_map (thứ tự ngẫu nhiên) nên dùng
                    // resolveAllSymbols (đã sắp xếp) và gọi cho mọi lib có export
                    // — nếu chỉ gọi 1 lib tùy ý, lib vẽ thật (vd libtriangle_gles)
                    // có thể không bao giờ được khởi tạo.
                    auto jniOnLoads = manager.resolveAllSymbols("JNI_OnLoad");
                    if (!jniOnLoads.empty()) {
                        // Log qua pipeline (android_logs.txt) chứ không chỉ buffer —
                        // run 21/08 chết giữa "Symbol JNI_OnLoad resolved" và
                        // "Found JNI_OnLoad" mà không có crash.log (không qua
                        // handler) → cần marker granular để biết chết đúng chỗ.
                        kudroid_android_log_message(4, "kudroid_core",
                                                    "run_apk: init main thread TLS before JNI_OnLoad");
                        bionic_init_main_thread_tls();
                        kudroid_android_log_message(4, "kudroid_core",
                                                    "run_apk: TLS done, invoking JNI_OnLoad");
                        for (const auto& [libName, addr] : jniOnLoads) {
                            auto jni_onload = reinterpret_cast<jint (*)(JavaVM*, void*)>(addr);
                            char invokeMsg[512];
                            snprintf(invokeMsg, sizeof(invokeMsg),
                                     "run_apk: invoking JNI_OnLoad in %s (addr=%p)",
                                     libName.c_str(), (void*)addr);
                            kudroid_android_log_message(4, "kudroid_core", invokeMsg);
                            jint version = jni_onload(jvm, nullptr);
                            char retMsg[512];
                            snprintf(retMsg, sizeof(retMsg),
                                     "run_apk: JNI_OnLoad(%s) returned %d",
                                     libName.c_str(), (int)version);
                            kudroid_android_log_message(4, "kudroid_core", retMsg);
                            log += "[kudroid_core] Found JNI_OnLoad in " + libName + ", invoking...\n";
                            mirrorCrash(log);
                            log += "[kudroid_core] JNI_OnLoad(" + libName + ") returned version: " + std::to_string(version) + "\n";
                            mirrorCrash(log);
                        }
                    } else {
                        log += "[kudroid_core] JNI_OnLoad not found.\n";
                        mirrorCrash(log);
                    }

                    auto native_activity_create = reinterpret_cast<void (*)(ANativeActivity*, void*, size_t)>(
                        manager.resolveAppSymbol("ANativeActivity_onCreate")
                    );
                    if (native_activity_create) {
                        log += "[kudroid_core] Found ANativeActivity_onCreate, invoking...\n";
                        mirrorCrash(log);

                        // thiết lập các lệnh gọi lại hoạt động để trò chơi có thể bắt đầu
                        // kết xuất và nhận đầu vào. đây là các hook
                        // mà trò chơi đăng ký các trình xử lý.
                        static ANativeActivityCallbacks mock_callbacks = {};
                        static ANativeActivity mock_activity = {
                            &mock_callbacks,
                            jvm,
                            nullptr, // env
                            nullptr, // clazz
                            "/sdcard/Android/data/test", // internalDataPath
                            "/sdcard/Android/data/test", // externalDataPath
                            29, // sdkVersion
                            nullptr, // instance
                            nullptr, // assetManager
                            nullptr  // obbPath
                        };
                        kudroid_jni_get_env(jvm, reinterpret_cast<void**>(&mock_activity.env), 0);

                        // assetManager phải là object Java thật (lớp framework
                        // android/content/res/AssetManager) — game gọi
                        // AAssetManager_fromJava(env, activity->assetManager); nếu
                        // nullptr, nhiều engine (Unity) bỏ qua mọi asset.
                        if (mock_activity.env) {
                            JNIEnv* env = mock_activity.env;
                            // mock_activity là static — chạy APK lần 2 (hoặc chạy
                            // lại game) sẽ ghi đè assetManager. GlobalRef cũ phải
                            // được thả, nếu không rò rỉ ref mỗi lần chạy (đến giới
                            // hạn GlobalRef table là JVM abort). KHÔNG thả ngay sau
                            // run_apk: game có thể còn giữ jobject này (activity
                            // sống hết vòng đời) nên chỉ thả ref cũ trước khi tạo mới.
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
                                    log += "[kudroid_core] AssetManager jobject created.\n";
                                }
                                if (env->ExceptionCheck()) env->ExceptionClear();
                                env->DeleteLocalRef(assetCls);
                            } else {
                                if (env->ExceptionCheck()) env->ExceptionClear();
                            }
                        }

                        // gọi oncreate. trò chơi lưu trữ trạng thái của nó trong
                        // activity->instance và đăng ký các lệnh gọi lại của nó.
                        native_activity_create(&mock_activity, nullptr, 0);
                        log += "[kudroid_core] ANativeActivity_onCreate completed.\n";
                        mirrorCrash(log);

                        // điều khiển vòng đời để trò chơi bắt đầu kết xuất:
                        // onstart -> onresume -> onwindowfocuschanged(true)
                        // -> onnativewindowcreated -> oninputqueuecreated.
                        //
                        // QUAN TRỌNG: mỗi callback có signature riêng trong NDK.
                        // Trước đây tất cả bị cast về void(*)(ANativeActivity*) và gọi
                        // với 1 tham số — game đọc tham số 2 (window/focus/queue) từ
                        // register rác. Truyền đúng số tham số cho từng loại.
                        auto call1 = [&](const char* name, void* fn) {
                            if (!fn) return;
                            log += std::string("[kudroid_core] Calling ") + name + "\n";
                            mirrorCrash(log);
                            reinterpret_cast<void (*)(ANativeActivity*)>(fn)(&mock_activity);
                        };
                        auto call2 = [&](const char* name, void* fn, void* arg2) {
                            if (!fn) return;
                            log += std::string("[kudroid_core] Calling ") + name + "\n";
                            mirrorCrash(log);
                            reinterpret_cast<void (*)(ANativeActivity*, void*)>(fn)(&mock_activity, arg2);
                        };
                        call1("onStart", mock_callbacks.onStart);
                        call1("onResume", mock_callbacks.onResume);
                        // onWindowFocusChanged(activity, hasFocus)
                        call2("onWindowFocusChanged", mock_callbacks.onWindowFocusChanged,
                              reinterpret_cast<void*>(1));
                        // onNativeWindowCreated(activity, ANativeWindow*)
                        call2("onNativeWindowCreated", mock_callbacks.onNativeWindowCreated,
                              g_metalLayer);
                        // onInputQueueCreated(activity, AInputQueue*)
                        call2("onInputQueueCreated", mock_callbacks.onInputQueueCreated,
                              kudroid_get_input_queue());
                        log += "[kudroid_core] Lifecycle callbacks invoked.\n";
                        mirrorCrash(log);
                    } else {
                        log += "[kudroid_core] ANativeActivity_onCreate not found.\n";
                        mirrorCrash(log);
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

extern "C" int kudroid_delete_app(const char* package_name) {
    if (!package_name) return 0;
    
    const std::string androidRoot = kudroid::VFSPathRemapper::getInstance().androidRoot();
    std::filesystem::path appCodePath = std::filesystem::path(androidRoot) / "data/app" / package_name;
    std::filesystem::path appDataPath = std::filesystem::path(androidRoot) / "data/data" / package_name;
    
    std::error_code ec;
    int success = 1;
    if (std::filesystem::exists(appCodePath, ec)) {
        if (std::filesystem::remove_all(appCodePath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    if (std::filesystem::exists(appDataPath, ec)) {
        if (std::filesystem::remove_all(appDataPath, ec) == static_cast<std::uintmax_t>(-1)) {
            success = 0;
        }
    }
    return success;
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
                jint version = jniOnLoad(kudroid_jni_get_javavm(), nullptr);
                log += "[kudroid_jni] JNI_OnLoad returned version: 0x" + std::to_string(version) + "\n";
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
                kudroid_jni_set_log_callback([](const char* msg) {
                    if (g_jni_test_log) {
                        *g_jni_test_log += "[kudroid_jni] ";
                        *g_jni_test_log += msg;
                        *g_jni_test_log += "\n";
                    }
                });

                kudroid_jni_init_jvm("", ""); // đảm bảo nó được khởi tạo
                mirrorCrash(log);
                void* vm = kudroid_jni_get_javavm();
                
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
#include "kudroid/DexManager.h"

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
    std::string targetDir = std::string(g_logDir) + "/extracted_apk";
    
    log += "[kudroid_apk] Initializing VFS...\n";
    kudroid::VFSPathRemapper::getInstance().setDocumentsDirectory(g_logDir);
    
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

    log += "[kudroid_apk] Loading DEX files...\n";
    kudroid::DexManager::getInstance().loadDirectory(targetDir);
    
    log += "[kudroid_apk] Scanning for native libraries (.so)...\n";
    // Process-lifetime (xem globalLibraryManager) — JNI_OnLoad dưới đây spawn
    // render thread guest; mappings phải ở lại sau khi hàm này return.
    kudroid::LibraryManager& libManager = globalLibraryManager();
    std::string libDir = targetDir + "/lib/arm64-v8a";

    // ── PIPELINE DEX→JAR AOT CACHING ───────────────────────────────────────
    // Avian không hiểu .dex: dịch classes*.dex → classes.jar cache tại
    //   <g_logDir>/data/dalvik-cache/<appName>/   (appName = tên file APK)
    // khóa bằng SHA-256 (cache.hash); jar cache đưa vào boot classpath lúc init.
    const std::string aotAppName = std::filesystem::path(apkStr).stem().string();
    const std::filesystem::path aotCacheDir =
        std::filesystem::path(g_logDir) / "data/dalvik-cache" / aotAppName;
    std::string aotError;
    const std::string aotClassesJar = kudroid::DexAotCache::translate_dex_if_needed(
        targetDir, aotCacheDir.string(), &aotError);
    if (!aotClassesJar.empty()) {
        log += "[kudroid_apk] DEX→JAR AOT ready: " + aotClassesJar + "\n";
    } else {
        log += "[kudroid_apk] WARNING: DEX→JAR AOT skipped (" + aotError + ")\n";
    }
    
    if (std::filesystem::exists(libDir)) {
        // Android gọi JNI_OnLoad cho TỪNG lib được loadLibrary — theo dõi lib
        // nào đã gọi để mỗi lib chỉ khởi tạo một lần (resolveGlobalSymbol cũ trả
        // 1 lib tùy ý theo thứ tự unordered_map — có thể bỏ sót lib vẽ thật).
        std::set<std::string> onLoadCalled;
        for (const auto& entry : std::filesystem::directory_iterator(libDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".so") {
                log += "[kudroid_apk] Loading library: " + entry.path().filename().string() + "\n";
                if (!libManager.loadRecursive(entry.path().string())) {
                    log += "[kudroid_apk] WARNING: Failed to load " + entry.path().filename().string() + "\n";
                } else {
                    log += "[kudroid_apk] Loaded successfully.\n";
                    // cố gắng gọi jni_onload — JVM phải được khởi tạo TRƯỚC, nếu
                    // không kudroid_jni_get_javavm() trả nullptr và JNI_OnLoad crash.
                    if (!aotClassesJar.empty()) {
                        kudroid_jni_init_jvm("", aotClassesJar.c_str());
                    } else {
                        kudroid_jni_init_jvm("", "");
                    }
                    JavaVM* jvm = kudroid_jni_get_javavm();
                    if (!jvm) {
                        log += "[kudroid_apk] ERROR: Avian JVM failed to initialize, skipping JNI_OnLoad\n";
                    } else {
                        for (const auto& [libKey, addr] : libManager.resolveAllSymbols("JNI_OnLoad")) {
                            if (!onLoadCalled.insert(libKey).second) continue; // đã gọi rồi
                            log += "[kudroid_apk] Found JNI_OnLoad in " + libKey + ", executing...\n";
                            using JNI_OnLoad_t = jint (*)(JavaVM*, void*);
                            JNI_OnLoad_t jniOnLoad = reinterpret_cast<JNI_OnLoad_t>(addr);
                            jint version = jniOnLoad(jvm, nullptr);
                            log += "[kudroid_apk] JNI_OnLoad(" + libKey + ") returned version: 0x" + std::to_string(version) + "\n";
                        }
                    }
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
// dịch dex → jar (có bộ đệm)
//
// dịch một tệp dex thành một tệp jar của các lớp giả (thông qua dextojar), sử dụng
// dexcachemanager để lưu trữ kết quả được khóa bằng mã băm dex + phiên bản công cụ.
// trả về một chuỗi nhật ký được malloc; người gọi phải giải phóng nó bằng free().
// ─────────────────────────────────────────────────────────────────────────────
extern "C" const char* kudroid_translate_dex(const char* dexPath) {
    if (!dexPath || !*dexPath) {
        return strdup("[kudroid_dex] ERROR: null DEX path\n");
    }

    // phiên bản công cụ — tăng số này khi logic trình biên dịch thay đổi.
    const int kDexToolVersion = 1;

    std::string log;
    log += "[kudroid_dex] Translating DEX: " + std::string(dexPath) + "\n";

    auto& cache = kudroid::DexCacheManager::getInstance();
    if (cache.cacheDirectory().empty()) {
        // thư mục bộ đệm mặc định: documents/android_cache (được thiết lập thông qua kudroid_set_log_dir).
        if (g_logDir[0]) {
            cache.setCacheDirectory(std::string(g_logDir) + "/android_cache");
        }
    }

    std::vector<uint8_t> jar;
    std::string error;
    if (!cache.translateAndCache(dexPath, kDexToolVersion, jar, &error)) {
        log += "[kudroid_dex] TRANSLATE FAILED: " + error + "\n";
        writeLogFile("kudroid_dex_translate.txt", log);
        return strdup(log.c_str());
    }

    log += "[kudroid_dex] Translation OK: " + std::to_string(jar.size()) + " bytes JAR\n";
    log += "[kudroid_dex] Cache dir: " + cache.cacheDirectory() + "\n";

    // ghi tệp jar vào đĩa để avian có thể tải nó dưới dạng đường dẫn lớp.
    std::string jarPath = std::string(g_logDir) + "/translated_classes.jar";
    FILE* f = std::fopen(jarPath.c_str(), "wb");
    if (f) {
        std::fwrite(jar.data(), 1, jar.size(), f);
        std::fclose(f);
        log += "[kudroid_dex] Wrote JAR: " + jarPath + "\n";
    } else {
        log += "[kudroid_dex] WARNING: cannot write JAR to " + jarPath + "\n";
    }

    writeLogFile("kudroid_dex_translate.txt", log);
    return strdup(log.c_str());
}