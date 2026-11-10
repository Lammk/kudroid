// ---- Linux libc entry points the host libc lacks. ----

// posix_fadvise: an access-pattern hint. The only thing to apply on this host is read-ahead;
// the descriptor and arguments are still validated as POSIX requires.
extern "C" int bionic_posix_fadvise(int fd, off_t offset, off_t len, int advice) {
    // Linux advice values: NORMAL 0, RANDOM 1, SEQUENTIAL 2, WILLNEED 3, DONTNEED 4, NOREUSE 5.
    if (offset < 0 || len < 0 || advice < 0 || advice > 5) return EINVAL;
    struct stat st;
    if (::fstat(fd, &st) != 0) return errno;
    if (S_ISFIFO(st.st_mode)) return ESPIPE;
#if defined(__APPLE__)
    if (advice == 1 || advice == 2 || advice == 3) {
        ::fcntl(fd, F_RDAHEAD, advice == 1 ? 0 : 1);
    }
#endif
    return 0;
}

// posix_fallocate: guarantee the range exists, growing the file when it ends past EOF.
// Returns an error number rather than -1, as POSIX specifies.
extern "C" int bionic_posix_fallocate(int fd, off_t offset, off_t len) {
    if (offset < 0 || len <= 0) return EINVAL;
    struct stat st;
    if (::fstat(fd, &st) != 0) return errno;
    if (S_ISDIR(st.st_mode)) return EISDIR;
    if (!S_ISREG(st.st_mode)) return ENODEV;
    off_t end = 0;
    if (__builtin_add_overflow(offset, len, &end)) return EFBIG;
    if (end <= st.st_size) return 0;
#if defined(__APPLE__)
    // Reserve the space first so ENOSPC is reported now instead of at write time.
    fstore_t store = {F_ALLOCATEALL, F_PEOFPOSMODE, 0, end - st.st_size, 0};
    if (::fcntl(fd, F_PREALLOCATE, &store) == -1 && errno == ENOSPC) return ENOSPC;
#endif
    if (::ftruncate(fd, end) != 0) return errno;
    return 0;
}

// vmsplice: with no page-gifting on this host it degrades to the copying form, which is a
// plain writev/readv on the pipe end.
extern "C" ssize_t bionic_vmsplice(int fd, const struct iovec* iov, unsigned long nr_segs,
                                   unsigned int flags) {
    (void)flags;
    if (iov == nullptr && nr_segs != 0) { errno = EFAULT; return -1; }
    if (nr_segs > static_cast<unsigned long>(IOV_MAX)) { errno = EINVAL; return -1; }
    struct stat st;
    if (::fstat(fd, &st) != 0) return -1;
    if (!S_ISFIFO(st.st_mode)) { errno = EBADF; return -1; }
    const int access = ::fcntl(fd, F_GETFL) & O_ACCMODE;
    return access == O_RDONLY ? ::readv(fd, iov, static_cast<int>(nr_segs))
                              : ::writev(fd, iov, static_cast<int>(nr_segs));
}

// sched_setscheduler / sched_getparam: app processes cannot use real-time policies (the
// platform refuses with EPERM), and every other policy is time-sharing with priority 0.
extern "C" int bionic_sched_getparam(pid_t pid, void* param) {
    if (param == nullptr || pid < 0) { errno = EINVAL; return -1; }
    *static_cast<int*>(param) = 0;
    return 0;
}

extern "C" int bionic_sched_setscheduler(pid_t pid, int policy, const void* param) {
    if (param == nullptr || pid < 0) { errno = EINVAL; return -1; }
    const int priority = *static_cast<const int*>(param);
    constexpr int kResetOnFork = 0x40000000;
    switch (policy & ~kResetOnFork) {
        case 0: // SCHED_OTHER
        case 3: // SCHED_BATCH
        case 5: // SCHED_IDLE
            if (priority != 0) { errno = EINVAL; return -1; }
            return 0;
        case 1: // SCHED_FIFO
        case 2: // SCHED_RR
            if (priority < 1 || priority > 99) { errno = EINVAL; return -1; }
            errno = EPERM;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }
}

// mkstemp: the template is a guest path, so it goes through the VFS like open(); the six
// generated characters are copied back to the guest's own template.
extern "C" int bionic_mkstemp(char* tmpl) {
    if (tmpl == nullptr) { errno = EINVAL; return -1; }
    const std::string remapped = kudroid::VFSPathRemapper::getInstance().remap(tmpl);
    std::vector<char> work(remapped.begin(), remapped.end());
    work.push_back('\0');
    const int fd = ::mkstemp(work.data());
    if (fd >= 0) {
        const size_t guestLen = std::strlen(tmpl);
        const size_t hostLen = work.size() - 1;
        if (guestLen >= 6 && hostLen >= 6) {
            std::memcpy(tmpl + guestLen - 6, work.data() + hostLen - 6, 6);
        }
    }
    return fd;
}

// tdestroy: free every node of a tsearch tree, giving each key to free_node first.
extern "C" void bionic_tdestroy(void* root, void (*free_node)(void*)) {
    if (root == nullptr) return;
#if defined(__APPLE__)
    // Nodes cannot be freed while twalk is still reading them, so collect them first. A node is
    // reported once, as a leaf or at its end-order visit, and its first member is the key.
    thread_local std::vector<const void*>* collected = nullptr;
    std::vector<const void*> nodes;
    std::vector<const void*>* previous = collected;
    collected = &nodes;
    ::twalk(root, [](const void* node, VISIT which, int) {
        if (which == endorder || which == leaf) collected->push_back(node);
    });
    collected = previous;
    for (const void* node : nodes) {
        if (free_node != nullptr) free_node(*static_cast<void* const*>(node));
        ::free(const_cast<void*>(node));
    }
#else
    ::tdestroy(root, free_node);
#endif
}

// ---- mntent: mount table iteration over a (VFS-mapped) text file. ----

struct GuestMntent {
    char* mnt_fsname;
    char* mnt_dir;
    char* mnt_type;
    char* mnt_opts;
    int mnt_freq;
    int mnt_passno;
};

// Decodes "\040"-style octal escapes in place.
static void mntent_unescape(char* field) {
    char* out = field;
    for (const char* in = field; *in != '\0';) {
        if (in[0] == '\\' && in[1] >= '0' && in[1] <= '3' && in[2] >= '0' && in[2] <= '7' &&
            in[3] >= '0' && in[3] <= '7') {
            *out++ = static_cast<char>(((in[1] - '0') << 6) | ((in[2] - '0') << 3) | (in[3] - '0'));
            in += 4;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
}

// Splits one fstab/mtab line into `ent` (pointing into `line`). False for blank and comment
// lines or lines with fewer than the four mandatory fields.
extern "C" bool kudroid_parse_mntent_line(char* line, GuestMntent* ent) {
    size_t n = std::strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '\0' || *p == '#') return false;
    char* fields[4] = {nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 4; ++i) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') return false;
        fields[i] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') ++p;
        if (*p != '\0') *p++ = '\0';
        mntent_unescape(fields[i]);
    }
    int numbers[2] = {0, 0};
    for (int i = 0; i < 2; ++i) {
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '\0') break;
        char* end = nullptr;
        numbers[i] = static_cast<int>(std::strtol(p, &end, 10));
        if (end == p) break;
        p = end;
    }
    ent->mnt_fsname = fields[0];
    ent->mnt_dir = fields[1];
    ent->mnt_type = fields[2];
    ent->mnt_opts = fields[3];
    ent->mnt_freq = numbers[0];
    ent->mnt_passno = numbers[1];
    return true;
}

extern "C" void* bionic_setmntent(const char* path, const char* mode) {
    if (path == nullptr || mode == nullptr) { errno = EINVAL; return nullptr; }
    const std::string remapped = kudroid::VFSPathRemapper::getInstance().remap(path);
    return ::fopen(remapped.c_str(), mode);
}

extern "C" GuestMntent* bionic_getmntent_r(void* stream, GuestMntent* ent, char* buf, int buflen) {
    FILE* fp = static_cast<FILE*>(stream);
    if (fp == nullptr || ent == nullptr || buf == nullptr || buflen <= 1) return nullptr;
    while (::fgets(buf, buflen, fp) != nullptr) {
        const size_t len = std::strlen(buf);
        if (len + 1 == static_cast<size_t>(buflen) && buf[len - 1] != '\n') {
            // The line did not fit; drop the rest of it rather than parse a fragment.
            int c;
            while ((c = ::fgetc(fp)) != EOF && c != '\n') {
            }
            continue;
        }
        if (kudroid_parse_mntent_line(buf, ent)) return ent;
    }
    return nullptr;
}

extern "C" int bionic_endmntent(void* stream) {
    if (stream != nullptr) ::fclose(static_cast<FILE*>(stream));
    return 1;
}

extern "C" void bionic___assert2(const char* file, int line, const char* function, const char* message) {
    char buf[512];
    snprintf(buf, sizeof(buf), "ASSERTION FAILED in %s (%s:%d): %s",
             function ? function : "?", file ? file : "?", line, message ? message : "");
    logAndroidMessage(6, "KuDroidAssert", buf);
    fprintf(stderr, "[KuDroidAssert] %s\n", buf);
    kudroid_store_abort_message(buf);
    bionic_abort();
}

// Report a guest assertion failure and abort, as bionic does.
//
// Split from the entry point below so the arm64 path can reach it after unpacking the
// guest's registers itself: __android_log_assert is variadic, and forwarding a guest's
// varargs to a host vsnprintf reads the wrong place (see GuestVarargs.h). The failure was
// visible in a crash log — strlen faulting on 0x3930 inside this function, which is a
// format argument that was never a pointer.
static void reportGuestAssert(const char* cond, const char* tag, const char* message) {
    logAndroidMessage(6, tag ? tag : "KuDroidAssert", message);
    fprintf(stderr, "[KuDroidAssert][%s] %s\n", tag ? tag : "assert", message);
    // The condition text is what names the failing invariant; bionic prints it too, and
    // without it the message alone often does not say which check tripped.
    if (cond && *cond) {
        fprintf(stderr, "[KuDroidAssert] condition: %s\n", cond);
    }
    abort();
}

#if defined(__aarch64__)
extern "C" void kudroid_log_assert_trampoline();

// __android_log_assert(cond, tag, fmt, ...): varargs start at the fourth integer register.
extern "C" int kudroid_log_assert_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    const char* cond = reinterpret_cast<const char*>(registers->gp[0]);
    const char* tag = reinterpret_cast<const char*>(registers->gp[1]);
    const char* format = reinterpret_cast<const char*>(registers->gp[2]);

    char message[512];
    if (format != nullptr) {
        FormatGuestVarargs(message, sizeof(message), format, registers, /*firstGpIndex=*/3);
    } else {
        FormatGuestVarargs(message, sizeof(message), "assertion failed", registers, 3);
    }
    reportGuestAssert(cond, tag, message);
    return 0;  // not reached: reportGuestAssert aborts
}

extern "C" const char* kudroid_guest_syslog_error();
extern "C" void kudroid_guest_syslog_write(int priority, const char* message);

// Format a guest varargs call into a malloc'd string the guest later free()s.
static int allocGuestFormatted(char** strp, const char* format, const GuestVarargs* registers,
                               unsigned firstGpIndex) {
    if (strp == nullptr) return -1;
    const char* fmt = format != nullptr ? format : "";
    // Format once to learn the length, then again into a buffer that fits.
    char probe[1024];
    const size_t needed = FormatGuestVarargs(probe, sizeof(probe), fmt, registers, firstGpIndex);
    char* buffer = static_cast<char*>(std::malloc(needed + 1));
    if (buffer == nullptr) { *strp = nullptr; return -1; }
    if (needed < sizeof(probe)) {
        __builtin_memcpy(buffer, probe, needed + 1);
    } else {
        FormatGuestVarargs(buffer, needed + 1, fmt, registers, firstGpIndex);
    }
    *strp = buffer;
    return static_cast<int>(needed);
}

// Write a formatted string to a descriptor through the shim's own write, so guest fds map
// the way they do for write().
static int writeGuestFormatted(int fd, const char* format, const GuestVarargs* registers,
                               unsigned firstGpIndex) {
    char* text = nullptr;
    const int length = allocGuestFormatted(&text, format, registers, firstGpIndex);
    if (length < 0 || text == nullptr) return -1;
    size_t done = 0;
    while (done < static_cast<size_t>(length)) {
        const ssize_t n = bionic_write(fd, text + done, static_cast<size_t>(length) - done);
        if (n <= 0) { std::free(text); return -1; }
        done += static_cast<size_t>(n);
    }
    std::free(text);
    return length;
}

// asprintf(strp, format, ...)
extern "C" int kudroid_asprintf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    return allocGuestFormatted(reinterpret_cast<char**>(registers->gp[0]),
                               reinterpret_cast<const char*>(registers->gp[1]), registers, 2);
}

// __asprintf_chk(strp, flag, format, ...)
extern "C" int kudroid_asprintf_chk_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    return allocGuestFormatted(reinterpret_cast<char**>(registers->gp[0]),
                               reinterpret_cast<const char*>(registers->gp[2]), registers, 3);
}

// dprintf(fd, format, ...)
extern "C" int kudroid_dprintf_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    return writeGuestFormatted(static_cast<int>(registers->gp[0]),
                               reinterpret_cast<const char*>(registers->gp[1]), registers, 2);
}

// __dprintf_chk(fd, flag, format, ...)
extern "C" int kudroid_dprintf_chk_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    return writeGuestFormatted(static_cast<int>(registers->gp[0]),
                               reinterpret_cast<const char*>(registers->gp[2]), registers, 3);
}

// __android_log_buf_print(bufID, priority, tag, format, ...)
extern "C" int kudroid_log_buf_print_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    const int priority = static_cast<int>(registers->gp[1]);
    const char* tag = reinterpret_cast<const char*>(registers->gp[2]);
    const char* format = reinterpret_cast<const char*>(registers->gp[3]);
    char message[1024];
    FormatGuestVarargs(message, sizeof(message), format != nullptr ? format : "", registers, 4);
    return logAndroidMessage(priority, tag, message);
}

// __syslog_chk(priority, flag, format, ...)
extern "C" void kudroid_syslog_chk_from_registers(const uint64_t* frame) {
    const auto* registers = reinterpret_cast<const GuestVarargs*>(frame);
    char message[2048];
    FormatGuestVarargs(message, sizeof(message),
                       reinterpret_cast<const char*>(registers->gp[2]), registers, 3, 0,
                       kudroid_guest_syslog_error());
    kudroid_guest_syslog_write(static_cast<int>(registers->gp[0]), message);
}
#else
extern "C" void bionic___android_log_assert(const char* cond, const char* tag, const char* fmt, ...) {
    char buf[512];
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
    } else {
        snprintf(buf, sizeof(buf), "assertion failed: %s", cond ? cond : "unknown");
    }
    reportGuestAssert(cond, tag, buf);
}
#endif

extern "C" ssize_t bionic_splice(int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags) {
    (void)flags;
    char buf[16384];
    size_t total = 0;
    while (total < len) {
        size_t chunk = std::min(sizeof(buf), len - total);
        ssize_t n = off_in ? ::pread(fd_in, buf, chunk, *off_in) : ::read(fd_in, buf, chunk);
        if (n <= 0) break;
        if (off_in) *off_in += n;
        ssize_t w = off_out ? ::pwrite(fd_out, buf, n, *off_out) : ::write(fd_out, buf, n);
        if (w <= 0) break;
        if (off_out) *off_out += w;
        total += w;
        if (w != n) break;
    }
    return total > 0 ? static_cast<ssize_t>(total) : -1;
}

extern "C" ssize_t bionic_copy_file_range(int fd_in, off_t* off_in, int fd_out, off_t* off_out, size_t len, unsigned int flags) {
    return bionic_splice(fd_in, off_in, fd_out, off_out, len, flags);
}

extern "C" int bionic_omp_in_parallel() {
    return 0; // cha c OpenMP runtime — "ngoi vng parallel"
}

extern "C" char* bionic___strchr_chk(const char* s, int c, size_t dst_len) {
    if (!s) return nullptr;
    for (size_t i = 0; i < dst_len; ++i) {
        if (s[i] == static_cast<char>(c)) return const_cast<char*>(&s[i]);
        if (s[i] == '\0') return nullptr;
    }
    return nullptr;
}

// bionic __strncpy_chk2: copy at most n chars, bounded by src_len/dst_len (fortify).
extern "C" char* bionic___strncpy_chk2(char* dst, const char* src, size_t n,
                                       size_t dst_len, size_t src_len) {
    if (!dst || !src) return dst;
    size_t copy = n;
    if (copy > dst_len) { copy = dst_len; }
    if (src_len < copy) { copy = src_len; }
    if (copy > 0) std::memcpy(dst, src, copy);
    // strncpy pads the remainder with 0 when space remains.
    if (n > copy && dst_len > copy) std::memset(dst + copy, 0, std::min(n - copy, dst_len - copy));
    return dst;
}

extern "C" void bionic___FD_CLR_chk(int fd, fd_set* set) {
    if (fd < 0 || fd >= FD_SETSIZE || !set) {
        trace("__FD_CLR_chk: fd out of range");
        return;
    }
    FD_CLR(fd, set);
}

extern "C" struct cmsghdr* bionic___cmsg_nxthdr(struct msghdr* mhdr, struct cmsghdr* cmsg) {
    // msghdr/cmsghdr layout Linux v macOS ging nhau (msg_control@40,
    // msg_controllen@48; cmsghdr: len/level/type) nn CMSG_NXTHDR host read ng.
    return CMSG_NXTHDR(mhdr, cmsg);
}

