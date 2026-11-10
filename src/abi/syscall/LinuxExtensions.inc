// ============================================================================
// Additional Linux syscalls commonly used by games
// ============================================================================

// getcpu — return the current CPU and NUMA node.
extern "C" int bionic_getcpu(unsigned* cpu, unsigned* node, void* tcache) {
    (void)tcache;
#ifdef __APPLE__
    if (cpu) *cpu = 0;
    if (node) *node = 0;
    return 0;
#else
    return ::syscall(SYS_getcpu, cpu, node, tcache);
#endif
}

// sched_getaffinity — return the CPU affinity mask.
//
// This is the WRAPPER, not the raw syscall, so success is 0 and the caller reads
// the mask out of `mask`. The raw syscall (number 123 in bionic_syscall) has the
// opposite contract and returns the byte count instead; see the note there.
extern "C" int bionic_sched_getaffinity(pid_t pid, size_t cpusetsize, void* mask) {
    (void)pid;
    if (!mask || cpusetsize == 0) {
        errno = EINVAL;
        return -1;
    }
    // Zero the whole set, then report the online cores in the low word. Zeroing
    // everything is correct here because nothing downstream reinterprets the return value
    // as a length.
    //
    // The mask comes from the same topology as sysconf and /proc/cpuinfo. It was 0xFF —
    // eight cores, regardless of what the device had and regardless of what every other
    // surface reported.
    memset(mask, 0, cpusetsize);
    const unsigned long online = static_cast<unsigned long>(kudroid::cpu_online_mask());
    memcpy(mask, &online, cpusetsize < sizeof(online) ? cpusetsize : sizeof(online));
    return 0;
}

// sched_setaffinity — set the CPU affinity mask (no-op).
extern "C" int bionic_sched_setaffinity(pid_t pid, size_t cpusetsize, const void* mask) {
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
}

// __sched_cpucount — count the set bits in a cpu_set_t.
//
// This is the prime suspect for "Cores = 0". It was not in the shim table at all, so the
// ELF loader bound it to kudroid_universal_dummy, which returns 0 — and CPU_COUNT() is a
// macro over this function, so every guest asking how many CPUs its affinity mask names
// got zero no matter what the mask actually contained. That is the exact shape of Unity's
// "Cores = 0" alongside a mask this shim had filled in.
//
// bionic's signature is `int __sched_cpucount(size_t setsize, const cpu_set_t* set)`, and
// it counts over the CALLER's declared size rather than a fixed width: a guest with a
// larger cpu_set_t must have all of its words counted.
extern "C" int bionic___sched_cpucount(size_t setsize, const void* set) {
    if (set == nullptr || setsize == 0) return 0;
    const unsigned char* bytes = static_cast<const unsigned char*>(set);
    int count = 0;
    for (size_t i = 0; i < setsize; ++i) {
        count += __builtin_popcount(static_cast<unsigned>(bytes[i]));
    }
    return count;
}

// sched_getcpu — return 0; iOS has no query and 0 is always a valid index.
extern "C" int bionic_sched_getcpu(void) {
#ifdef __APPLE__
    return 0;
#else
    const int cpu = ::sched_getcpu();
    return cpu >= 0 ? cpu : 0;
#endif
}

// get_nprocs / get_nprocs_conf — core-count helpers sharing the sysconf source.
extern "C" int bionic_get_nprocs(void) {
    return static_cast<int>(kudroid::query_cpu_topology().total_cores);
}

extern "C" int bionic_get_nprocs_conf(void) {
    return static_cast<int>(kudroid::query_cpu_topology().total_cores);
}

// get_phys_pages / get_avphys_pages — memory in pages.
extern "C" long bionic_get_phys_pages(void) {
    long pz = ::sysconf(_SC_PAGESIZE);
    if (pz <= 0) pz = 16384;
    return static_cast<long>(kudroid::query_system_memory().total_bytes /
                             static_cast<uint64_t>(pz));
}

extern "C" long bionic_get_avphys_pages(void) {
    long pz = ::sysconf(_SC_PAGESIZE);
    if (pz <= 0) pz = 16384;
    return static_cast<long>(kudroid::query_system_memory().available_bytes /
                             static_cast<uint64_t>(pz));
}

// inotify / signalfd — not on iOS; emulate with a pollable fd that never fires.
extern "C" int bionic_inotify_init1(int flags) {
    (void)flags;
#ifdef __APPLE__
    const int fd = create_loopback_udp();
#else
    const int fd = ::inotify_init1(flags);
#endif
    if (fd >= 0) {
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(fd);
    }
    logAndroidMessage(4, "KuDroidSyscall", "inotify_init1 -> fd=" + std::to_string(fd) + " (emulated)");
    return fd;
}

// inotify_add_watch — register a watch (emulated: returns dummy descriptor).
extern "C" int bionic_inotify_add_watch(int fd, const char* pathname, uint32_t mask) {
    (void)fd; (void)pathname; (void)mask;
    static int s_next_wd = 1;
    const int wd = s_next_wd++;
    logAndroidMessage(4, "KuDroidSyscall", "inotify_add_watch(\"" +
                      std::string(pathname ? pathname : "<null>") + "\", mask=0x" +
                      [](uint32_t m){ char b[16]; snprintf(b, sizeof(b), "%x", (unsigned)m); return std::string(b); }(mask) +
                      ") -> wd=" + std::to_string(wd) + " (emulated)");
    return wd;
}

// inotify_rm_watch — remove a watch (emulated success).
extern "C" int bionic_inotify_rm_watch(int fd, int wd) {
    (void)fd; (void)wd;
    return 0;
}

// signalfd — create a file descriptor for signal delivery (emulated: fd poll-able,
// no pending signals — poll loop returns without blocking.
extern "C" int bionic_signalfd(int fd, const void* mask, int flags) {
    (void)mask; (void)flags;
    if (fd >= 0) return fd; // signalfd(fd,...) with existing fd — reuse
#ifdef __APPLE__
    const int newfd = create_loopback_udp();
#else
    const int newfd = ::signalfd(-1, reinterpret_cast<const sigset_t*>(mask), flags);
#endif
    if (newfd >= 0) {
        std::lock_guard<std::mutex> lock(g_fakefds_mtx);
        g_fakefds.insert(newfd);
    }
    logAndroidMessage(4, "KuDroidSyscall", "signalfd -> fd=" + std::to_string(newfd) + " (emulated)");
    return newfd;
}

// eventfd2 — same as eventfd (already implemented as bionic_eventfd).
extern "C" int bionic_eventfd2(unsigned int initval, int flags) {
    return bionic_eventfd(initval, flags);
}

// prlimit64 — get/set process resource limits.
// Previously returned 0 without filling old_limit -> uninitialized struct rlimit.
// Now populated from host getrlimit with Linux->Darwin resource index mapping.
struct GuestRlimit64 { uint64_t rlim_cur; uint64_t rlim_max; }; // Linux rlimit64
extern "C" int bionic_prlimit64(pid_t pid, int resource, const void* new_limit, void* old_limit) {
    (void)pid;
#ifdef __APPLE__
    // Linux RLIMIT_* -> Darwin RLIMIT_* (index mapping)
    // Linux: CPU=0 FSIZE=1 DATA=2 STACK=3 CORE=4 RSS=5 NPROC=6 NOFILE=7
    //        MEMLOCK=8 AS=9 LOCKS=10 SIGPENDING=11 MSGQUEUE=12 NICE=13 RTPRIO=14
    // Darwin: CPU=0 FSIZE=1 DATA=2 STACK=3 CORE=4 AS=5 MEMLOCK=6 NPROC=7 NOFILE=8
    int darwin_resource = -1;
    switch (resource) {
        case 0: darwin_resource = RLIMIT_CPU; break;      // 0 == 0
        case 1: darwin_resource = RLIMIT_FSIZE; break;    // 1 == 1
        case 2: darwin_resource = RLIMIT_DATA; break;     // 2 == 2
        case 3: darwin_resource = RLIMIT_STACK; break;    // 3 == 3
        case 4: darwin_resource = RLIMIT_CORE; break;     // 4 == 4
        case 6: darwin_resource = RLIMIT_NPROC; break;    // 6 -> 7
        case 7: darwin_resource = RLIMIT_NOFILE; break;   // 7 -> 8
        case 8: darwin_resource = RLIMIT_MEMLOCK; break;  // 8 -> 6
        case 9: darwin_resource = RLIMIT_AS; break;       // 9 -> 5
        default: darwin_resource = -1; break;             // RSS/LOCKS/... unsupported on Darwin
    }
#else
    const int darwin_resource = resource;
#endif
    struct rlimit host;
    if (darwin_resource < 0 || ::getrlimit(darwin_resource, &host) != 0) {
        if (old_limit) {
            auto* out = static_cast<GuestRlimit64*>(old_limit);
            out->rlim_cur = out->rlim_max = 0;
        }
        return 0; // unmapped resource — return 0 gracefully
    }
    if (new_limit) {
        const auto* in = static_cast<const GuestRlimit64*>(new_limit);
        host.rlim_cur = static_cast<rlim_t>(in->rlim_cur);
        host.rlim_max = static_cast<rlim_t>(in->rlim_max);
        ::setrlimit(darwin_resource, &host);
    }
    if (old_limit) {
        auto* out = static_cast<GuestRlimit64*>(old_limit);
        out->rlim_cur = static_cast<uint64_t>(host.rlim_cur);
        out->rlim_max = static_cast<uint64_t>(host.rlim_max);
    }
    return 0;
}

// statx — extended stat. Emulated via host stat() into Linux UAPI struct statx.
struct GuestStatxTimestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};
struct GuestStatx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    GuestStatxTimestamp stx_atime, stx_btime, stx_ctime, stx_mtime;
    uint32_t stx_rdev_major, stx_rdev_minor;
    uint32_t stx_dev_major, stx_dev_minor;
    uint64_t stx_mnt_id;
    uint32_t stx_dio_mem_align;
    uint32_t stx_dio_offset_align;
    uint64_t __spare3[12];
};
static_assert(sizeof(GuestStatx) == 256, "statx layout must be 256 bytes (Linux UAPI)");

static void fill_statx_from_stat(struct GuestStatx* sx, const struct stat& st) {
    std::memset(sx, 0, sizeof(*sx));
    sx->stx_mask = 0x7ff; // STATX_BASIC_STATS
    sx->stx_blksize = uint32_t(st.st_blksize);
    sx->stx_nlink = uint32_t(st.st_nlink);
    sx->stx_uid = st.st_uid;
    sx->stx_gid = st.st_gid;
    sx->stx_mode = uint16_t(st.st_mode);
    sx->stx_ino = uint64_t(st.st_ino);
    sx->stx_size = uint64_t(st.st_size);
    sx->stx_blocks = uint64_t(st.st_blocks);
#ifdef __APPLE__
    sx->stx_atime = {st.st_atimespec.tv_sec, uint32_t(st.st_atimespec.tv_nsec), 0};
    sx->stx_ctime = {st.st_ctimespec.tv_sec, uint32_t(st.st_ctimespec.tv_nsec), 0};
    sx->stx_mtime = {st.st_mtimespec.tv_sec, uint32_t(st.st_mtimespec.tv_nsec), 0};
#else
    sx->stx_atime = {st.st_atim.tv_sec, uint32_t(st.st_atim.tv_nsec), 0};
    sx->stx_ctime = {st.st_ctim.tv_sec, uint32_t(st.st_ctim.tv_nsec), 0};
    sx->stx_mtime = {st.st_mtim.tv_sec, uint32_t(st.st_mtim.tv_nsec), 0};
#endif
    sx->stx_dev_major = uint32_t(st.st_dev >> 8);
    sx->stx_dev_minor = uint32_t(st.st_dev & 0xff);
}

extern "C" int bionic_statx(int dirfd, const char* pathname, int flags, unsigned mask, void* statxbuf) {
    if (!pathname || !statxbuf) { errno = EFAULT; return -1; }
    constexpr int syncFlags = 0x6000;
    if ((flags & syncFlags) == syncFlags || (mask & 0x80000000u)) { errno = EINVAL; return -1; }
    struct stat st;
    const int rc = guest_stat_at(dirfd, pathname, &st, flags & ~syncFlags);
    if (rc != 0) return -1; // errno already set
    fill_statx_from_stat(static_cast<GuestStatx*>(statxbuf), st);
    return 0;
}

