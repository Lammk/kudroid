
// Forward android.util.Log.println_native to standard KuDroid log pipeline.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message) {
    return logAndroidMessage(priority, tag, std::string(message ? message : ""));
}

// ─────────────────────────────────────────────────────────────────────────────
// Real FORTIFY (__*_chk) implementations to prevent silent data corruption.
//
// The size the compiler passes is the object size it measured for the destination.
// bionic aborts on a violation; KuDroid is a translator and does not get to kill the
// guest, so the write is clamped to that bound instead — the same thing the strncpy
// wrapper below has always done. Without the clamp the wrapper's own diagnostic is the
// only thing that changes: it reports the overflow and then performs it, which lands on
// the guest's heap as corruption that surfaces later as an unrelated crash.
//
// (size_t)-1 means "size unknown": a trailing flexible-array member or a pointer whose
// object size could not be computed both report that value, and clamping to it would
// truncate a legitimate copy. The call then goes through unchanged, which is also what
// _FORTIFY_SOURCE documents for an unknown size.
// ─────────────────────────────────────────────────────────────────────────────

static bool fortifyHaveBound(size_t declared_len) {
    return declared_len != static_cast<size_t>(-1);
}

extern "C" void* bionic___memcpy_chk(void* dst, const void* src, size_t n, size_t dst_len) {
    if (fortifyHaveBound(dst_len) && n > dst_len) {
        trace("__memcpy_chk: destination overflow (fortify), clamped");
        n = dst_len;
    }
    return ::memcpy(dst, src, n);
}

extern "C" void* bionic___memmove_chk(void* dst, const void* src, size_t n, size_t dst_len) {
    if (fortifyHaveBound(dst_len) && n > dst_len) {
        trace("__memmove_chk: destination overflow (fortify), clamped");
        n = dst_len;
    }
    return ::memmove(dst, src, n);
}

extern "C" void* bionic___memset_chk(void* s, int c, size_t n, size_t s_len) {
    if (fortifyHaveBound(s_len) && n > s_len) {
        trace("__memset_chk: destination overflow (fortify), clamped");
        n = s_len;
    }
    return ::memset(s, c, n);
}

extern "C" ssize_t bionic___read_chk(int fd, void* buf, size_t nbytes, size_t buflen) {
    (void)buflen;
    return ::read(fd, buf, nbytes);
}

extern "C" ssize_t bionic___write_chk(int fd, const void* buf, size_t count, size_t buflen) {
    (void)buflen;
    return ::write(fd, buf, count);
}

extern "C" int bionic___snprintf_chk(char* s, size_t maxlen, int flag, size_t slen,
                                     const char* format, ...) {
    (void)flag; (void)slen;
    va_list args;
    va_start(args, format);
    const int r = ::vsnprintf(s, maxlen, format, args);
    va_end(args);
    return r;
}

extern "C" int bionic___sprintf_chk(char* s, int flag, size_t slen, const char* format, ...) {
    (void)flag;
    va_list args;
    va_start(args, format);
    // slen == (size_t)-1 means the compiler could not size the destination, and the
    // documented behaviour is then an unbounded format. Handing that value to vsnprintf
    // instead is a size the host cannot honour: it fails the call (or returns -1) rather
    // than writing the string the guest asked for.
    const int r = slen == static_cast<size_t>(-1) ? ::vsprintf(s, format, args)
                                                  : ::vsnprintf(s, slen, format, args);
    va_end(args);
    return r;
}

// Fortified string copies: Unity's error path builds messages from a JNI
// string whose chars came back NULL (GetStringUTFChars on a cleared
// exception), and host strncpy traps on the null source. An empty string is
// what the copy would have produced from missing data — and exactly what the
// caller's snprintf/append expects to find.
extern "C" char* bionic___strncpy_chk(char* dst, const char* src, size_t n, size_t dst_len) {
    if (!dst) return nullptr;
    if (!src) {
        if (dst_len > 0) dst[0] = '\0';
        return dst;
    }
    if (fortifyHaveBound(dst_len) && n > dst_len) {
        trace("__strncpy_chk: destination overflow (fortify), clamped");
        n = dst_len; // clamp to prevent buffer overflow
    }
    return ::strncpy(dst, src, n);
}

extern "C" char* bionic___strcpy_chk(char* dst, const char* src, size_t dst_len) {
    if (!dst) return nullptr;
    if (!src) {
        if (dst_len > 0) dst[0] = '\0';
        return dst;
    }
    // Needs strlen(src) + 1 bytes, so the string itself must be shorter than dst_len.
    if (fortifyHaveBound(dst_len) && ::strlen(src) >= dst_len) {
        trace("__strcpy_chk: destination overflow (fortify), truncated");
        if (dst_len == 0) return dst;
        std::memcpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
        return dst;
    }
    return ::strcpy(dst, src);
}

extern "C" char* bionic___strcat_chk(char* dst, const char* src, size_t dst_len) {
    if (!dst) return nullptr;
    if (!src) return dst;
    if (fortifyHaveBound(dst_len)) {
        const size_t have = ::strlen(dst);
        if (have + ::strlen(src) >= dst_len) {
            trace("__strcat_chk: destination overflow (fortify), truncated");
            if (have + 1 >= dst_len) return dst;  // no room left for more than the NUL
            std::memcpy(dst + have, src, dst_len - have - 1);
            dst[dst_len - 1] = '\0';
            return dst;
        }
    }
    return ::strcat(dst, src);
}

extern "C" unsigned long bionic___fdelt_chk(unsigned long fd) {
    if (fd >= FD_SETSIZE) {
        trace("__fdelt_chk: fd exceeds FD_SETSIZE (fortify)");
    }
    return fd / (8UL * sizeof(unsigned long));
}

// bionic: void android_set_abort_message(const char* msg)
// Store guest abort messages for tombstone crash logs (e.g. Unity FATAL messages).
extern "C" void bionic_android_set_abort_message(const char* msg) {
    if (msg && *msg) {
        kudroid_store_abort_message(msg);
        char traceMessage[256];
        snprintf(traceMessage, sizeof(traceMessage),
                 "android_set_abort_message: %.200s", msg);
        trace(traceMessage);
    }
}

extern "C" uint64_t bionic_ZSTD_trace_compress_begin(const void* cctx) {
    (void)cctx;
    return 0;
}

extern "C" void bionic_ZSTD_trace_compress_end(uint64_t handle, const void* cctx) {
    (void)handle;
    (void)cctx;
}

extern "C" uint64_t bionic_ZSTD_trace_decompress_begin(const void* dctx) {
    (void)dctx;
    return 0;
}

extern "C" void bionic_ZSTD_trace_decompress_end(uint64_t handle, const void* dctx) {
    (void)handle;
    (void)dctx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bionic malloc extensions queried from host libmalloc.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" size_t bionic_malloc_usable_size(const void* ptr) {
    if (!ptr) return 0;
#if defined(__APPLE__)
    return ::malloc_size(ptr);
#else
    return ::malloc_usable_size(const_cast<void*>(ptr));
#endif
}

// Bionic struct mallinfo (10 size_t entries returned by value).
struct BionicMallinfo {
    size_t arena;     // total non-mmap heap space
    size_t ordblks;   // number of free blocks
    size_t smblks;    // always 0 on Bionic
    size_t hblks;     // number of mmap regions
    size_t hblkhd;    // total mmap bytes
    size_t usmblks;   // always 0
    size_t fsmblks;   // always 0
    size_t uordblks;  // total allocated bytes
    size_t fordblks;  // total free bytes
    size_t keepcost;  // reclaimable bytes
};

extern "C" BionicMallinfo bionic_mallinfo(void) {
    BionicMallinfo mi = {};
#if defined(__APPLE__)
    malloc_statistics_t st = {};
    ::malloc_zone_statistics(::malloc_default_zone(), &st);
    mi.arena = st.size_allocated;
    mi.hblks = st.blocks_in_use;
    mi.hblkhd = st.max_size_in_use;
    mi.uordblks = st.size_in_use;
    mi.fordblks = st.size_allocated > st.size_in_use
                      ? st.size_allocated - st.size_in_use : 0;
    mi.keepcost = mi.fordblks;
    mi.ordblks = 1;
#endif
    return mi;
}

// jemalloc sized-delete: size is an optimization hint, mapped to standard free().
extern "C" void bionic_sdallocx(void* ptr, size_t size, int flags) {
    (void)size;
    (void)flags;
    ::free(ptr);
}

// BoringSSL memory hooks mapped to malloc/free.
extern "C" void* bionic_OPENSSL_memory_alloc(size_t size) {
    return ::malloc(size);
}

extern "C" void bionic_OPENSSL_memory_free(void* ptr) {
    ::free(ptr);
}

extern "C" size_t bionic_OPENSSL_memory_get_size(void* ptr) {
    return bionic_malloc_usable_size(ptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// memfd_create emulation via immediate unlinked temporary file in NSTemporaryDirectory.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int bionic_memfd_create(const char* name, unsigned int flags) {
    const char* tmp = ::getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    char path[PATH_MAX];
    static std::atomic<unsigned> counter{0};
    std::snprintf(path, sizeof(path), "%s/kudroid-memfd-%s-%d-%u", tmp,
                  (name && *name) ? name : "anon", static_cast<int>(::getpid()),
                  counter.fetch_add(1, std::memory_order_relaxed));
    const int fd = ::open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return -1;
    ::unlink(path);
#ifdef FD_CLOEXEC
    // MFD_CLOEXEC == 1 in bionic.
    if (flags & 1u) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    return fd;
}

// ─────────────────────────────────────────────────────────────────────────────
// Two-parameter pthread_setname_np adapted for Darwin.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int bionic_pthread_setname_np2(pthread_t thread, const char* name) {
#if defined(__APPLE__)
    // Darwin pthread_setname_np only sets current thread name.
    if (::pthread_equal(thread, ::pthread_self())) {
        return ::pthread_setname_np(name ? name : "");
    }
    return 0;
#else
    // Linux host matches Bionic signature directly.
    return ::pthread_setname_np(thread, name ? name : "");
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// mallopt/malloc_info stubs for Darwin host.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" int bionic_mallopt(int param, int value) {
    (void)param; (void)value;
    return 1;
}

extern "C" int bionic_malloc_info(int options, FILE* fp) {
    (void)options;
    if (!fp) { errno = EINVAL; return -1; }
    errno = ENOSYS;
    return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// setprogname/getprogname fallback using static buffer on Linux hosts.
// ─────────────────────────────────────────────────────────────────────────────

static char g_progName[256] = "";

extern "C" void bionic_setprogname(const char* name) {
#if defined(__APPLE__)
    ::setprogname(name);
#else
    if (name) {
        std::strncpy(g_progName, name, sizeof(g_progName) - 1);
        g_progName[sizeof(g_progName) - 1] = '\0';
    }
#endif
}

extern "C" const char* bionic_getprogname() {
#if defined(__APPLE__)
    return ::getprogname();
#else
    if (g_progName[0]) return g_progName;
    // Default: argv[0] basename from /proc/self/cmdline.
    static char buf[256] = "unknown";
    FILE* f = std::fopen("/proc/self/cmdline", "rb");
    if (f) {
        size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        if (n > 0) {
            buf[n] = '\0';
            char* slash = std::strrchr(buf, '/');
            if (slash) std::memmove(buf, slash + 1, std::strlen(slash + 1) + 1);
        }
    }
    return buf;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// pthread_gettid_np — retrieve tid for specific thread.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" pid_t bionic_pthread_gettid_np(pthread_t thread) {
#if defined(__APPLE__)
    uint64_t tid = 0;
    if (::pthread_threadid_np(thread, &tid) != 0) return -1;
    return static_cast<pid_t>(tid);
#else
    if (::pthread_equal(thread, ::pthread_self())) return bionic_gettid();
    return -1; // Linux has no direct API to get tid from non-current pthread_t
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// __pthread_cleanup_push/pop: per-thread cleanup handler stack.
// ─────────────────────────────────────────────────────────────────────────────

struct BionicCleanup {
    BionicCleanup* prev;
    void (*routine)(void*);
    void* arg;
};

static thread_local BionicCleanup* g_cleanupHead = nullptr;

extern "C" void bionic___pthread_cleanup_push(BionicCleanup* c,
                                              void (*routine)(void*), void* arg) {
    if (!c) return;
    c->routine = routine;
    c->arg = arg;
    c->prev = g_cleanupHead;
    g_cleanupHead = c;
}

extern "C" void bionic___pthread_cleanup_pop(BionicCleanup* c, int execute) {
    if (!c) return;
    g_cleanupHead = c->prev;
    if (execute && c->routine) c->routine(c->arg);
}

// ─────────────────────────────────────────────────────────────────────────────
// __fgets_chk — bounds-checked fgets (FORTIFY).
// ─────────────────────────────────────────────────────────────────────────────

extern "C" char* bionic___fgets_chk(char* dst, int supplied_size, FILE* stream,
                                    size_t dst_len_from_compiler) {
    if (!dst || !stream || supplied_size <= 0) return nullptr;
    if (dst_len_from_compiler != static_cast<size_t>(-1) &&
        static_cast<size_t>(supplied_size) > dst_len_from_compiler) {
        // Real buffer overflow — abort like Bionic rather than corrupting memory.
        bionic___assert2(__FILE__, __LINE__, "__fgets_chk",
                         "fgets: prevented write past end of buffer");
        return nullptr;
    }
    return ::fgets(dst, supplied_size, stream);
}

