typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
#define NULL ((void*)0)

// Bionic Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "HardcoreSyscall"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// Bionic standard functions
extern long syscall(long number, ...);
extern void* malloc(size_t size);
extern void free(void* ptr);
extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);
extern int memcmp(const void* s1, const void* s2, size_t n);
extern size_t strlen(const char* s);
extern int strcmp(const char* s1, const char* s2);

// ARM64 Linux Syscall Numbers
#define __NR_epoll_create1 20
#define __NR_epoll_ctl 21
#define __NR_epoll_pwait 22
#define __NR_dup 23
#define __NR_dup3 24
#define __NR_mkdirat 34
#define __NR_unlinkat 35
#define __NR_ftruncate 46
#define __NR_openat 56
#define __NR_close 57
#define __NR_pipe2 59
#define __NR_lseek 62
#define __NR_read 63
#define __NR_write 64
#define __NR_pread64 67
#define __NR_pwrite64 68
#define __NR_newfstatat 79
#define __NR_futex 98
#define __NR_nanosleep 101
#define __NR_clock_gettime 113
#define __NR_sched_yield 124
#define __NR_uname 160
#define __NR_prctl 167
#define __NR_gettimeofday 169
#define __NR_getpid 172
#define __NR_getuid 174
#define __NR_gettid 178
#define __NR_socket 198
#define __NR_munmap 215
#define __NR_mmap 222
#define __NR_mprotect 226
#define __NR_madvise 233
#define __NR_getrandom 278

// File flags
#define AT_FDCWD -100
#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_CLOEXEC 02000000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Memory Protection & Flags
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MADV_DONTNEED 4
#define MADV_WILLNEED 3

// Futex Operations
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

// Clock types
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCK_BOOTTIME 7

// Socket
#define AF_INET 2
#define SOCK_DGRAM 2
#define SOCK_STREAM 1

// Epoll
#define EPOLL_CTL_ADD 1
#define EPOLLIN 0x001

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct stat_buf {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

struct epoll_event_dummy {
    uint32_t events;
    uint64_t data;
};

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, name) do { \
    if (cond) { \
        LOGI("  [PASS] %s", name); \
        g_passCount++; \
    } else { \
        LOGE("  [FAIL] %s (Line %d)", name, __LINE__); \
        g_failCount++; \
    } \
} while(0)

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🏛️ [KUDROID HARDCORE FULL SYSCALL TEST SUITE]");
    LOGI("=================================================");

    g_passCount = 0;
    g_failCount = 0;

    // ──────────────────────────────────────────────
    // 1. MEMORY MANAGEMENT SYSCALLS (mmap, mprotect, munmap, madvise)
    // ──────────────────────────────────────────────
    LOGI("🧪 [GROUP 1] Memory Management Syscalls...");
    size_t pageSize = 4096;
    size_t mapSize = pageSize * 4;
    void* mapped = (void*)syscall(__NR_mmap, NULL, mapSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(mapped != (void*)-1 && mapped != NULL, "mmap anonymous RW 16KB");

    if (mapped && mapped != (void*)-1) {
        // Ghi dữ liệu vào trang bộ nhớ
        uint8_t* ptr = (uint8_t*)mapped;
        ptr[0] = 0xAA;
        ptr[pageSize] = 0xBB;
        ptr[pageSize * 3 + 10] = 0xCC;
        TEST_ASSERT(ptr[0] == 0xAA && ptr[pageSize] == 0xBB && ptr[pageSize * 3 + 10] == 0xCC, "mmap write & read verification");

        long madvRes = syscall(__NR_madvise, mapped, mapSize, MADV_WILLNEED);
        TEST_ASSERT(madvRes == 0, "madvise(MADV_WILLNEED)");

        long mprotRes = syscall(__NR_mprotect, mapped, mapSize, PROT_READ);
        TEST_ASSERT(mprotRes == 0, "mprotect(PROT_READ)");
        TEST_ASSERT(ptr[0] == 0xAA, "mprotect readable verified");

        // Đổi lại RW để test munmap
        syscall(__NR_mprotect, mapped, mapSize, PROT_READ | PROT_WRITE);
        long munmapRes = syscall(__NR_munmap, mapped, mapSize);
        TEST_ASSERT(munmapRes == 0, "munmap 16KB");
    }

    // ──────────────────────────────────────────────
    // 2. PROCESS, THREAD, IDENTITY & FUTEX
    // ──────────────────────────────────────────────
    LOGI("🧪 [GROUP 2] Process, Thread, Identity & Futex...");
    long pid = syscall(__NR_getpid);
    long tid = syscall(__NR_gettid);
    long uid = syscall(__NR_getuid);
    TEST_ASSERT(pid > 0, "getpid > 0");
    TEST_ASSERT(tid > 0, "gettid > 0");
    TEST_ASSERT(uid >= 0, "getuid >= 0");

    long yieldRes = syscall(__NR_sched_yield);
    TEST_ASSERT(yieldRes == 0, "sched_yield");

    // prctl thread name
    char threadName[16] = "KuDroidWorker";
    long prctlSet = syscall(__NR_prctl, 15 /* PR_SET_NAME */, (uintptr_t)threadName, 0, 0, 0);
    TEST_ASSERT(prctlSet == 0, "prctl(PR_SET_NAME)");

    char getNameBuf[16] = {0};
    long prctlGet = syscall(__NR_prctl, 16 /* PR_GET_NAME */, (uintptr_t)getNameBuf, 0, 0, 0);
    TEST_ASSERT(prctlGet == 0 && strcmp(getNameBuf, "KuDroidWorker") == 0, "prctl(PR_GET_NAME) matches set name");

    // Futex wake test (wake 0 threads on integer)
    int futexVal = 1;
    long futexWake = syscall(__NR_futex, &futexVal, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0);
    TEST_ASSERT(futexWake >= 0, "futex(FUTEX_WAKE) returns >= 0");

    // ──────────────────────────────────────────────
    // 3. TIME, CLOCKS & TIMERS
    // ──────────────────────────────────────────────
    LOGI("🧪 [GROUP 3] Time, Clocks & Timers...");
    struct timespec tsReal = {0};
    struct timespec tsMono = {0};
    struct timespec tsBoot = {0};
    long clkReal = syscall(__NR_clock_gettime, CLOCK_REALTIME, &tsReal);
    long clkMono = syscall(__NR_clock_gettime, CLOCK_MONOTONIC, &tsMono);
    long clkBoot = syscall(__NR_clock_gettime, CLOCK_BOOTTIME, &tsBoot);

    TEST_ASSERT(clkReal == 0 && tsReal.tv_sec > 1700000000, "clock_gettime(CLOCK_REALTIME) valid epoch");
    TEST_ASSERT(clkMono == 0 && (tsMono.tv_sec > 0 || tsMono.tv_nsec > 0), "clock_gettime(CLOCK_MONOTONIC) valid");
    TEST_ASSERT(clkBoot == 0, "clock_gettime(CLOCK_BOOTTIME) supported");

    struct timeval tv = {0};
    long gtd = syscall(__NR_gettimeofday, &tv, NULL);
    TEST_ASSERT(gtd == 0 && tv.tv_sec > 1700000000, "gettimeofday valid seconds");

    struct timespec sleepReq = { 0, 5000000 }; // 5ms
    long nsRes = syscall(__NR_nanosleep, &sleepReq, NULL);
    TEST_ASSERT(nsRes == 0, "nanosleep(5ms)");

    // ──────────────────────────────────────────────
    // 4. FILE SYSTEM & I/O (openat, write, read, pread, pwrite, ftruncate, stat, dup, pipe2, unlink)
    // ──────────────────────────────────────────────
    LOGI("🧪 [GROUP 4] File System & I/O Syscalls...");
    const char* testFilePath = "/data/local/tmp/kudroid_syscall_test_file.bin";
    long fd = syscall(__NR_openat, AT_FDCWD, testFilePath, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    TEST_ASSERT(fd >= 0, "openat(O_RDWR | O_CREAT)");

    if (fd >= 0) {
        const char testPayload[] = "KuDroid Hypervisor Hardcore Syscall Test Payload 2026";
        size_t payloadLen = sizeof(testPayload);
        ssize_t written = (ssize_t)syscall(__NR_write, fd, testPayload, payloadLen);
        TEST_ASSERT(written == (ssize_t)payloadLen, "write payload 54 bytes");

        long pos = syscall(__NR_lseek, fd, 0, SEEK_SET);
        TEST_ASSERT(pos == 0, "lseek(SEEK_SET, 0)");

        char readBuf[128] = {0};
        ssize_t bytesRead = (ssize_t)syscall(__NR_read, fd, readBuf, sizeof(readBuf));
        TEST_ASSERT(bytesRead == (ssize_t)payloadLen && memcmp(readBuf, testPayload, payloadLen) == 0, "read verifies written payload");

        // pread64 & pwrite64 (đọc ghi tại offset cụ thể mà không dời con trỏ lseek)
        const char pwritePayload[] = "OVERWRITE";
        ssize_t pw = (ssize_t)syscall(__NR_pwrite64, fd, pwritePayload, 9, (uint64_t)8);
        TEST_ASSERT(pw == 9, "pwrite64 at offset 8");

        char preadBuf[16] = {0};
        ssize_t pr = (ssize_t)syscall(__NR_pread64, fd, preadBuf, 9, (uint64_t)8);
        TEST_ASSERT(pr == 9 && memcmp(preadBuf, "OVERWRITE", 9) == 0, "pread64 at offset 8 matches");

        // fstatat / newfstatat
        struct stat_buf st = {0};
        long statRes = syscall(__NR_newfstatat, fd, "", &st, 0x1000 /* AT_EMPTY_PATH */);
        if (statRes != 0) {
            statRes = syscall(__NR_newfstatat, AT_FDCWD, testFilePath, &st, 0);
        }
        TEST_ASSERT(statRes == 0 && st.st_size > 0, "newfstatat returns valid file size");

        // dup & dup3
        long dupFd = syscall(__NR_dup, fd);
        TEST_ASSERT(dupFd >= 0 && dupFd != fd, "dup(fd)");
        if (dupFd >= 0) syscall(__NR_close, dupFd);

        syscall(__NR_close, fd);
        syscall(__NR_unlinkat, AT_FDCWD, testFilePath, 0);
    }

    // pipe2 non-blocking
    int pipeFds[2] = {-1, -1};
    long pipeRes = syscall(__NR_pipe2, pipeFds, 0x0004 /* O_NONBLOCK */ | O_CLOEXEC);
    TEST_ASSERT(pipeRes == 0 && pipeFds[0] >= 0 && pipeFds[1] >= 0, "pipe2(O_NONBLOCK | O_CLOEXEC)");
    if (pipeRes == 0) {
        syscall(__NR_write, pipeFds[1], "PIPE", 4);
        char pBuf[8] = {0};
        ssize_t pr = (ssize_t)syscall(__NR_read, pipeFds[0], pBuf, 4);
        TEST_ASSERT(pr == 4 && memcmp(pBuf, "PIPE", 4) == 0, "pipe read/write data transfer");
        syscall(__NR_close, pipeFds[0]);
        syscall(__NR_close, pipeFds[1]);
    }

    // ──────────────────────────────────────────────
    // 5. SOCKET & EPOLL (socket, epoll_create1, epoll_ctl, epoll_pwait)
    // ──────────────────────────────────────────────
    LOGI("🧪 [GROUP 5] Sockets & Epoll Multiplexing...");
    long sockFd = syscall(__NR_socket, AF_INET, SOCK_DGRAM, 0);
    TEST_ASSERT(sockFd >= 0, "socket(AF_INET, SOCK_DGRAM)");

    long epollFd = syscall(__NR_epoll_create1, 0x80000 /* EPOLL_CLOEXEC */);
    TEST_ASSERT(epollFd >= 0, "epoll_create1(EPOLL_CLOEXEC)");

    if (epollFd >= 0 && sockFd >= 0) {
        struct epoll_event_dummy ev;
        ev.events = EPOLLIN;
        ev.data = 12345ULL;
        long ctlRes = syscall(__NR_epoll_ctl, epollFd, EPOLL_CTL_ADD, sockFd, &ev);
        TEST_ASSERT(ctlRes == 0, "epoll_ctl(EPOLL_CTL_ADD)");

        struct epoll_event_dummy events[4];
        struct timespec epollTimeout = { 0, 1000000 }; // 1ms
        long pwaitRes = syscall(__NR_epoll_pwait, epollFd, events, 4, 1 /* 1ms */, NULL);
        TEST_ASSERT(pwaitRes >= 0, "epoll_pwait returns without crash");

        syscall(__NR_close, epollFd);
        syscall(__NR_close, sockFd);
    }

    // ──────────────────────────────────────────────
    // 6. SYSTEM & HARDWARE ENTROPY (uname, getrandom)
    // ──────────────────────────────────────────────
    LOGI("🧪 [GROUP 6] System & Entropy Syscalls...");
    struct utsname un = {0};
    long unameRes = syscall(__NR_uname, &un);
    TEST_ASSERT(unameRes == 0 && strlen(un.sysname) > 0, "uname returns valid sysname");
    LOGI("  ℹ️ Kernel Info: Sysname='%s', Release='%s', Machine='%s'", un.sysname, un.release, un.machine);

    uint8_t randomBytes[32] = {0};
    long randRes = syscall(__NR_getrandom, randomBytes, sizeof(randomBytes), 0);
    int nonZeroCount = 0;
    for (int i = 0; i < 32; ++i) if (randomBytes[i] != 0) nonZeroCount++;
    TEST_ASSERT(randRes == 32 && nonZeroCount > 10, "getrandom generates 32 entropy bytes");

    // ──────────────────────────────────────────────
    // TỔNG KẾT
    // ──────────────────────────────────────────────
    LOGI("=================================================");
    LOGI("📊 SYSCALL AUDIT REPORT: %d PASSED, %d FAILED (TOTAL %d)",
         g_passCount, g_failCount, g_passCount + g_failCount);
    LOGI("=================================================");

    return g_failCount == 0 ? 0 : 1;
}
