namespace kudroid {
namespace {

static void* bionic_thread_wrapper(void* rawArgs) {
    BionicThreadArgs* args = static_cast<BionicThreadArgs*>(rawArgs);
    void* (*start_routine)(void*) = args->start_routine;
    void* arg = args->arg;
    delete args;
    sync_diag("thread-entry", reinterpret_cast<void*>(start_routine), nullptr, 0);

#if defined(__aarch64__)
    apply_guest_fpcr();
#endif

    ::pthread_once(&tls_key_once, init_tls_key);

    // Allocate 64KB for Android TLS block and set tpidr_el0
    // Darwin uses tpidrro_el0, so tpidr_el0 is free for us!
    // Never fall back to plain aligned_alloc: it lacks the TLS template and guard cookie.
    void* tls_base = alloc_guest_tls_block();
    if (tls_base) {
        ::pthread_setspecific(tls_key, tls_base);
#if defined(__aarch64__)
        __asm__ volatile("msr tpidr_el0, %0" : : "r"((char*)tls_base + kTlsSlotOffset));
#endif
    }

    // iOS: ANGLE/Metal/ObjC require an autorelease pool on EVERY thread that
    // touches them. Guest render threads are raw pthreads with NO pool — GPU test passes because
    // it runs on the main/GCD thread (pool present). Without a pool → abort()
    // inside ANGLE eglInitialize (Metal device/queue creation).
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
#endif
    void* result = start_routine(arg);
#if defined(__APPLE__)
    objc_autoreleasePoolPop(pool);
#endif

    // No need to free(tls_base) here, the destructor will handle it automatically
    // when the thread terminates, even if it terminates via pthread_exit().
    sync_diag("thread-exit", reinterpret_cast<void*>(start_routine), nullptr, 0);
    return result;
}

extern "C" int bionic_pthread_create(pthread_t* thread, void* attr, void* (*start_routine)(void*), void* arg) {
    BionicThreadArgs* args = new BionicThreadArgs{start_routine, arg};

    // Ensure adequate guest thread stack size for runtime interpreters and deep engine frames.
    // Darwin default stack is only 512KB.
    constexpr size_t kMinGuestStackSize = 16 * 1024 * 1024;
    pthread_attr_t hostAttr;
    ::pthread_attr_init(&hostAttr);

    size_t stackSize = kMinGuestStackSize;
    if (attr) {
        const auto* a = static_cast<const BionicPthreadAttr*>(attr);
        if (a->stack_size > kMinGuestStackSize) {
            stackSize = a->stack_size;
        }
        if (a->flags & 0x1) { // PTHREAD_CREATE_DETACHED
            ::pthread_attr_setdetachstate(&hostAttr, PTHREAD_CREATE_DETACHED);
        }
    }
    stackSize = (stackSize + 16383) & ~static_cast<size_t>(16383);
    ::pthread_attr_setstacksize(&hostAttr, stackSize);

    const int res = ::pthread_create(thread, &hostAttr, bionic_thread_wrapper, args);
    ::pthread_attr_destroy(&hostAttr);
    if (res != 0) delete args;
    sync_diag("thread-create", reinterpret_cast<void*>(start_routine), nullptr, res);
    return res;
}

// pthread_join blocks until the target exits; track it as a join wait.
extern "C" int bionic_pthread_join(pthread_t thread, void** retval) {
    const BlockingWaitScope tracked(WaitKind::kJoin, reinterpret_cast<const void*>(thread),
                                    guest_return_address(6));
    return ::pthread_join(thread, retval);
}

// errno accessor: Darwin exports `__error`, glibc/musl export `__errno_location`.
// Guarded so SyscallShim.o actually links on a Linux host (previously any host
// consumer that pulled this object failed with an undefined `__error`).
#ifdef __APPLE__
extern "C" int* __error(void);
#else
extern "C" int* __errno_location(void);
#endif

// memalign — aligned memory allocation.
extern "C" void* bionic_memalign(size_t alignment, size_t size) {
    void* ptr = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    if (::posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
}

// __system_property_* — Android property system.
// prop_info is an opaque pointer; find returns it and read/callback consume it.
namespace {
struct KudroidProp {
    const char* name;
    const char* value;
};
const KudroidProp kKnownProps[] = {
    // Version and identity come from DeviceProfile.h so all surfaces agree.
    {"ro.build.version.sdk", KUDROID_SDK_INT_STR},
    {"ro.build.version.release", KUDROID_ANDROID_RELEASE},
    {"ro.build.version.codename", "REL"},
    {"ro.build.version.incremental", "6000000"},
    {"ro.build.type", "user"},
    {"ro.build.tags", "release-keys"},
    {"ro.build.fingerprint",
     KUDROID_DEVICE_BRAND "/" KUDROID_DEVICE_NAME "/" KUDROID_DEVICE_BOARD
     ":" KUDROID_ANDROID_RELEASE "/QP1A.190711.020/6000000:user/release-keys"},
    // KuDroid, not a spoofed Pixel, so apps avoid device-specific paths.
    {"ro.product.model", KUDROID_DEVICE_MODEL},
    {"ro.product.manufacturer", KUDROID_DEVICE_MANUFACTURER},
    {"ro.product.brand", KUDROID_DEVICE_BRAND},
    {"ro.product.device", KUDROID_DEVICE_BOARD},
    {"ro.product.name", KUDROID_DEVICE_NAME},
    {"ro.product.cpu.abi", KUDROID_DEVICE_ABI},
    {"ro.product.cpu.abilist", KUDROID_DEVICE_ABI},
    {"ro.product.cpu.abilist64", KUDROID_DEVICE_ABI},
    {"ro.hardware", "kudroid"},
    {"ro.board.platform", "kudroid"},
    {"ro.boot.hardware", "kudroid"},
    {"ro.sf.lcd_density", "480"},
    {"ro.opengles.version", "196610"}, // OpenGL ES 3.2 (0x00030002)
    {"ro.debuggable", "0"},
    {"persist.sys.timezone", "UTC"},
    {"persist.sys.locale", "en-US"},
    {"sys.boot_completed", "1"},
    {"gsm.version.baseband", "1.0"},
};
constexpr size_t kKnownPropsCount = sizeof(kKnownProps) / sizeof(kKnownProps[0]);

const KudroidProp* findProp(const char* name) {
    if (!name) return nullptr;
    for (size_t i = 0; i < kKnownPropsCount; ++i) {
        if (std::strcmp(kKnownProps[i].name, name) == 0) return &kKnownProps[i];
    }
    return nullptr;
}
} // namespace

int kudroid_get_dns_servers(char out[][64], int max) {
    int n = 0;
#if defined(__APPLE__)
    struct __res_state st;
    std::memset(&st, 0, sizeof(st));
    if (res_ninit(&st) == 0) {
        union res_sockaddr_union addrs[8];
        int got = res_getservers(&st, addrs, 8);
        for (int i = 0; i < got && n < max; ++i) {
            if (addrs[i].sin.sin_family == AF_INET) {
                ::inet_ntop(AF_INET, &addrs[i].sin.sin_addr, out[n], 64);
            } else if (addrs[i].sin6.sin6_family == AF_INET6) {
                ::inet_ntop(AF_INET6, &addrs[i].sin6.sin6_addr, out[n], 64);
            } else {
                continue;
            }
            if (out[n][0]) ++n;
        }
        res_ndestroy(&st);
    }
#endif
    static const char* const fallback[] = {"1.1.1.1", "8.8.8.8"};
    for (int i = 0; i < 2 && n < max; ++i) {
        std::snprintf(out[n++], 64, "%s", fallback[i]);
    }
    return n;
}

extern "C" int bionic_system_property_get(const char* name, char* value) {
    if (!name || !value) return 0;
    value[0] = '\0';
    if (std::strcmp(name, "net.dns1") == 0 || std::strcmp(name, "net.dns2") == 0 ||
        std::strcmp(name, "net.dns3") == 0 || std::strcmp(name, "net.dns4") == 0) {
        char servers[4][64];
        int count = kudroid_get_dns_servers(servers, 4);
        int idx = name[7] - '1';
        if (idx >= 0 && idx < count) {
            std::strncpy(value, servers[idx], 92);
            value[91] = '\0';
            return static_cast<int>(std::strlen(value));
        }
        return 0;
    }
    const KudroidProp* p = findProp(name);
    if (!p) return 0;
    std::strcpy(value, p->value);
    return static_cast<int>(std::strlen(p->value));
}

extern "C" const void* bionic_system_property_find(const char* name) {
    return findProp(name); // prop_info* — nullptr when missing
}

// int __system_property_read(const prop_info* pi, char* name, char* value)
extern "C" int bionic_system_property_read(const void* pi, char* name, char* value) {
    if (!pi) return 0;
    const KudroidProp* p = static_cast<const KudroidProp*>(pi);
    if (name) std::strcpy(name, p->name);
    if (value) std::strcpy(value, p->value);
    return static_cast<int>(std::strlen(p->value));
}

extern "C" void bionic_system_property_read_callback(
    void* pi, void (*callback)(void*, const char*, const char*, unsigned), void* cookie) {
    if (!pi || !callback) return;
    const KudroidProp* p = static_cast<const KudroidProp*>(pi);
    callback(cookie, p->name, p->value, 0u);
}

extern "C" int bionic_system_property_foreach(
    void (*callback)(const void*, void*), void* cookie) {
    if (!callback) {
        errno = EINVAL;
        return -1;
    }
    for (const auto& property : kKnownProps) callback(&property, cookie);
    return 0;
}

// dl_iterate_phdr — needed by the guest unwinder to find its exception tables.
extern "C" int bionic_dl_iterate_phdr(
    int (*callback)(void* info, size_t size, void* data), void* data) {
    return kudroid_iterate_guest_phdrs(callback, data);
}

// lseek64 — 64-bit file seek (on Darwin/iOS lseek is already 64-bit)
extern "C" off_t bionic_lseek64(int fd, off_t offset, int whence) {
    return ::lseek(fd, offset, whence);
}

// The v*printf family — receives an already-built guest va_list.
#if defined(__aarch64__)
// `va_list` in these signatures is the HOST type, but the value a guest passes is a
// pointer to its own va_list — so it is reinterpreted rather than used. Taking the address
// of the parameter would give the address of a local copy of the pointer, one level too
// deep; the parameter's VALUE is the guest's pointer.
static const void* guestVaListPointer(va_list ap) {
    const void* p = nullptr;
    __builtin_memcpy(&p, &ap, sizeof(p));
    return p;
}

extern "C" int bionic_android_log_vprint(int prio, const char* tag, const char* fmt,
                                         va_list ap) {
    char message[1024];
    kudroid_format_guest_va_list(message, sizeof(message), fmt, guestVaListPointer(ap));
    return logAndroidMessage(prio, tag, message);
}

extern "C" int bionic_vsnprintf(char* s, size_t size, const char* format, va_list ap) {
    return static_cast<int>(
        kudroid_format_guest_va_list(s, size, format, guestVaListPointer(ap)));
}

// vsprintf has no bound; the guest promises the buffer is large enough, exactly as it does
// on Android. The cap only limits how far a runaway format string can go.
extern "C" int bionic_vsprintf(char* s, const char* format, va_list ap) {
    return static_cast<int>(
        kudroid_format_guest_va_list(s, 0x10000, format, guestVaListPointer(ap)));
}

extern "C" int bionic_vfprintf(FILE* stream, const char* format, va_list ap) {
    char buffer[2048];
    const size_t would =
        kudroid_format_guest_va_list(buffer, sizeof(buffer), format, guestVaListPointer(ap));
    FILE* out = stream ? stream : stderr;
    if (out == stdout) {
        console_write_nonblocking(buffer, std::strlen(buffer));
    } else {
        std::fputs(buffer, out);
    }
    return static_cast<int>(would);
}

extern "C" int bionic_vprintf(const char* format, va_list ap) {
    return bionic_vfprintf(stdout, format, ap);
}

extern "C" int bionic_vasprintf(char** strp, const char* format, va_list ap) {
    if (strp == nullptr) return -1;
    // Format once to learn the length, exactly as the return contract allows, then once
    // more into a buffer that fits. The guest owns the result and frees it.
    char probe[1024];
    const void* guest = guestVaListPointer(ap);
    const size_t needed = kudroid_format_guest_va_list(probe, sizeof(probe), format, guest);
    char* buffer = static_cast<char*>(std::malloc(needed + 1));
    if (buffer == nullptr) { *strp = nullptr; return -1; }
    if (needed < sizeof(probe)) {
        __builtin_memcpy(buffer, probe, needed + 1);
    } else {
        kudroid_format_guest_va_list(buffer, needed + 1, format, guest);
    }
    *strp = buffer;
    return static_cast<int>(needed);
}

extern "C" int bionic___vasprintf_chk(char** strp, int flag, const char* format, va_list ap) {
    (void)flag;
    return bionic_vasprintf(strp, format, ap);
}

extern "C" int bionic_vdprintf(int fd, const char* format, va_list ap) {
    char* text = nullptr;
    if (bionic_vasprintf(&text, format, ap) < 0 || text == nullptr) return -1;
    const size_t length = std::strlen(text);
    size_t done = 0;
    while (done < length) {
        const ssize_t n = bionic_write(fd, text + done, length - done);
        if (n <= 0) { std::free(text); return -1; }
        done += static_cast<size_t>(n);
    }
    std::free(text);
    return static_cast<int>(length);
}

extern "C" int bionic___vdprintf_chk(int fd, int flag, const char* format, va_list ap) {
    (void)flag;
    return bionic_vdprintf(fd, format, ap);
}

extern "C" int bionic___vsnprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                      const char* format, va_list ap) {
    (void)flag; (void)slen;
    return static_cast<int>(
        kudroid_format_guest_va_list(s, maxlen, format, guestVaListPointer(ap)));
}

extern "C" int bionic___vsprintf_chk(char* s, int flag, size_t slen, const char* format,
                                     va_list ap) {
    (void)flag;
    return static_cast<int>(
        kudroid_format_guest_va_list(s, slen != 0 ? slen : 0x10000, format,
                                     guestVaListPointer(ap)));
}

extern "C" int bionic___vfprintf_chk(FILE* stream, int flag, const char* format,
                                     va_list ap) {
    (void)flag;
    return bionic_vfprintf(stream, format, ap);
}

// ── the scanf family ────────────────────────────────────────────────────────
//
// Same ABI split, opposite consequence: these WRITE through the guest's varargs. See
// GuestVarargs.h. The trampolines capture the registers; the character source for a FILE*
// lives here because GuestVarargs.cpp cannot include <cstdio>.
extern "C" int kudroid_sscanf_trampoline();
extern "C" int kudroid_isoc99_sscanf_trampoline();
extern "C" int kudroid_fscanf_trampoline();

namespace {
// ungetc is the only way to un-peek a FILE*, and it is guaranteed for one character —
// which is all the scanner's peek/advance interface needs.
int FilePeek(void* state) {
    FILE* f = static_cast<FILE*>(state);
    if (f == nullptr) return -1;
    const int c = std::fgetc(f);
    if (c == EOF) return -1;
    std::ungetc(c, f);
    return c;
}
void FileAdvance(void* state) {
    FILE* f = static_cast<FILE*>(state);
    if (f != nullptr) (void)std::fgetc(f);
}
}  // namespace

extern "C" int kudroid_fscanf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    FILE* stream = reinterpret_cast<FILE*>(registers->gp[0]);
    const char* format = reinterpret_cast<const char*>(registers->gp[1]);
    if (stream == nullptr || format == nullptr) return -1;
    return ScanGuestVarargsFrom(&FilePeek, &FileAdvance, stream, format, registers,
                                /*firstGpIndex=*/2, /*firstFpIndex=*/0);
}

extern "C" int bionic_vsscanf(const char* s, const char* format, va_list ap) {
    return kudroid_scan_guest_va_list(s, format, guestVaListPointer(ap));
}

extern "C" int bionic_vfscanf(FILE* stream, const char* format, va_list ap) {
    if (stream == nullptr || format == nullptr) return -1;
    GuestVarargs registers;
    unsigned firstGp = 0;
    unsigned firstFp = 0;
    if (!GuestVarargsFromVaList(guestVaListPointer(ap), &registers, &firstGp, &firstFp)) {
        return -1;
    }
    return ScanGuestVarargsFrom(&FilePeek, &FileAdvance, stream, format, &registers, firstGp,
                                firstFp);
}
#else
extern "C" int bionic_android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
    char message[1024];
    std::vsnprintf(message, sizeof(message), fmt ? fmt : "", ap);
    return logAndroidMessage(prio, tag, message);
}

extern "C" int bionic_vsscanf(const char* s, const char* format, va_list ap) {
    return ::vsscanf(s, format, ap);
}

extern "C" int bionic_vfscanf(FILE* stream, const char* format, va_list ap) {
    return ::vfscanf(stream ? stream : stdin, format, ap);
}

extern "C" int bionic_vsnprintf(char* s, size_t size, const char* format, va_list ap) {
    return ::vsnprintf(s, size, format, ap);
}

extern "C" int bionic_vsprintf(char* s, const char* format, va_list ap) {
    return ::vsprintf(s, format, ap);
}

extern "C" int bionic_vfprintf(FILE* stream, const char* format, va_list ap) {
    return ::vfprintf(stream ? stream : stderr, format, ap);
}

extern "C" int bionic_vprintf(const char* format, va_list ap) {
    return ::vprintf(format, ap);
}

extern "C" int bionic_vasprintf(char** strp, const char* format, va_list ap) {
    return ::vasprintf(strp, format, ap);
}

extern "C" int bionic___vasprintf_chk(char** strp, int flag, const char* format, va_list ap) {
    (void)flag;
    return ::vasprintf(strp, format, ap);
}

extern "C" int bionic_vdprintf(int fd, const char* format, va_list ap) {
    return ::vdprintf(fd, format, ap);
}

extern "C" int bionic___vdprintf_chk(int fd, int flag, const char* format, va_list ap) {
    (void)flag;
    return ::vdprintf(fd, format, ap);
}

extern "C" int bionic___vsnprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                      const char* format, va_list ap) {
    (void)flag; (void)slen;
    return ::vsnprintf(s, maxlen, format, ap);
}

extern "C" int bionic___vsprintf_chk(char* s, int flag, size_t slen, const char* format,
                                     va_list ap) {
    (void)flag;
    return ::vsnprintf(s, slen, format, ap);
}

extern "C" int bionic___vfprintf_chk(FILE* stream, int flag, const char* format,
                                     va_list ap) {
    (void)flag;
    return ::vfprintf(stream ? stream : stderr, format, ap);
}

// On a non-arm64 host the guest IS the host, so plain forwarding is correct.
extern "C" int bionic_sscanf(const char* s, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    const int r = ::vsscanf(s, format, ap);
    va_end(ap);
    return r;
}

extern "C" int bionic_fscanf(FILE* stream, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    const int r = ::vfscanf(stream ? stream : stdin, format, ap);
    va_end(ap);
    return r;
}
#endif

extern "C" const char* kudroid_guest_syslog_error() {
    const int saved = errno;
    const char* message = std::strerror(saved);
    errno = saved;
    return message;
}

extern "C" void kudroid_guest_syslog_write(int priority, const char* message) {
    const int saved = errno;
    ::syslog(priority, "%s", message);
    const int severity = priority & LOG_PRIMASK;
    const int androidPriority = severity <= LOG_ERR ? 6 :
        (severity == LOG_WARNING ? 5 : (severity <= LOG_INFO ? 4 : 3));
    logAndroidMessage(androidPriority, "syslog", message);
    errno = saved;
}

extern "C" void bionic_vsyslog(int priority, const char* format, va_list ap) {
    const int saved = errno;
    char message[2048];
#if defined(__aarch64__)
    kudroid::GuestVarargs registers;
    unsigned gp = 0, fp = 0;
    if (!kudroid::GuestVarargsFromVaList(guestVaListPointer(ap), &registers, &gp, &fp)) {
        errno = saved;
        return;
    }
    kudroid::FormatGuestVarargs(message, sizeof(message), format, &registers, gp, fp,
                               std::strerror(saved));
#else
    std::vsnprintf(message, sizeof(message), format, ap);
#endif
    kudroid_guest_syslog_write(priority, message);
    errno = saved;
}

extern "C" void bionic___vsyslog_chk(int priority, int flag, const char* format, va_list ap) {
    (void)flag;
    bionic_vsyslog(priority, format, ap);
}

#if !defined(__aarch64__)
extern "C" void bionic_syslog(int priority, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    bionic_vsyslog(priority, format, ap);
    va_end(ap);
}
#endif

extern "C" int bionic_android_log_write(int prio, const char* tag, const char* text) {
    // Routed through logAndroidMessage rather than stderr alone so a guest that logs via
    // __android_log_write lands in kudroid_android_logs.txt and the crash buffer with
    // everything else. It used to go only to stderr, which the crash log does not carry.
    return logAndroidMessage(prio, tag, text ? text : "");
}

extern "C" int bionic_android_log_buf_write(int bufId, int prio, const char* tag, const char* msg) {
    (void)bufId;
    return bionic_android_log_write(prio, tag, msg);
}

// ============================================================================
// __ctype_get_mb_cur_max — max bytes per multibyte character
// ============================================================================
extern "C" size_t bionic_ctype_get_mb_cur_max() {
    return 4; // UTF-8
}

// ============================================================================
// _ctype_ — character classification table (Bionic-compatible)
// ============================================================================
static const unsigned short g_bionic_ctype_[257] = {
    0, // EOF slot
    // 0x00-0x08: control chars
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    // 0x09: tab (control + blank + space)
    0x20 | 0x01 | 0x40,
    // 0x0A-0x0D: control + space (newline, vtab, formfeed, carriage return)
    0x20 | 0x01, 0x20 | 0x01, 0x20 | 0x01, 0x20 | 0x01,
    // 0x0E-0x1F: control
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    // 0x20: space
    0x01 | 0x40,
    // 0x21-0x2F: punctuation
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    // 0x30-0x39: digits
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    // 0x3A-0x40: punctuation
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    // 0x41-0x46: uppercase hex
    0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80, 0x02 | 0x80,
    // 0x47-0x5A: uppercase
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    // 0x5B-0x60: punctuation
    0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
    // 0x61-0x66: lowercase hex
    0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80, 0x08 | 0x80,
    // 0x67-0x7A: lowercase
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    // 0x7B-0x7E: punctuation
    0x10, 0x10, 0x10, 0x10,
    // 0x7F: DEL (control)
    0x20,
    // 0x80-0xFF: high bytes (0)
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Global pointer as expected by Android binaries for the data symbol
const unsigned short* g_ctype_ptr = &g_bionic_ctype_[1];

// ============================================================================
// pthread_condattr_setclock — set clock for condition variable
// ============================================================================
extern "C" int bionic_pthread_condattr_setclock(void* attr, int clock_id) {
    // Guest clock ids: 0 = CLOCK_REALTIME, 1 = CLOCK_MONOTONIC. Anything else is rejected.
    if (!attr || (clock_id != 0 && clock_id != 1)) return EINVAL;
    uint32_t word = 0;
    std::memcpy(&word, attr, sizeof(word));
    word = clock_id == 1 ? (word | kBionicCondClockMonotonic) : (word & ~kBionicCondClockMonotonic);
    std::memcpy(attr, &word, sizeof(word));
    return 0;
}

// ============================================================================
// Google internal stubs (used by Bionic internally, safe to no-op)
// ============================================================================
extern "C" void bionic_google_potentially_blocking_region_begin() {}
extern "C" void bionic_google_potentially_blocking_region_end() {}

// Dummy for __register_atfork
extern "C" int bionic_register_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void), void* dso_handle) {
    (void)prepare; (void)parent; (void)child; (void)dso_handle;
#ifdef __APPLE__
    return pthread_atfork(prepare, parent, child);
#else
    return 0; // iOS does not really support fork(), so just return success
#endif
}

