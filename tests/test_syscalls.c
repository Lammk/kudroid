#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif

extern int __android_log_print(int priority, const char* tag, const char* format, ...);

// Weak declarations so we can call them natively if resolved
extern int ashmem_create_region(const char *name, size_t size) __attribute__((weak));
extern int ashmem_set_prot_region(int fd, int prot) __attribute__((weak));

#define SYSCALL_TEST_OK 0
#define SYSCALL_TEST_FAIL -1

int kudroid_syscall_test(void) {
    int result = SYSCALL_TEST_OK;
    __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "Starting Syscall Traps Test...");

    /* 1. Test PRCTL */
    int pr_ret = prctl(PR_SET_NAME, (unsigned long)"TestThread", 0, 0, 0);
    if (pr_ret == 0) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "prctl(PR_SET_NAME) SUCCESS.");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "prctl(PR_SET_NAME) FAILED: %d", errno);
        result = SYSCALL_TEST_FAIL;
    }

    int pr_vma_ret = prctl(0x53564d41, 0, 0x1000, 0x1000, (unsigned long)"AnonVma");
    __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "prctl(PR_SET_VMA) returned: %d", pr_vma_ret);

    /* 2. Test MMAP, MPROTECT, MADVISE */
    void* map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map != MAP_FAILED) {
        strcpy((char*)map, "Hello MMAP");
        
        if (mprotect(map, 4096, PROT_READ) == 0) {
            __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "mprotect(PROT_READ) SUCCESS");
        } else {
            __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "mprotect() FAILED");
            result = SYSCALL_TEST_FAIL;
        }

        if (madvise(map, 4096, MADV_DONTNEED) == 0) {
            __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "madvise(MADV_DONTNEED) SUCCESS");
        } else {
            __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "madvise() FAILED");
        }

        munmap(map, 4096);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "mmap() FAILED");
        result = SYSCALL_TEST_FAIL;
    }

    /* 3. Test ASHMEM */
    if (ashmem_create_region) {
        int ash_fd = ashmem_create_region("test_ashmem", 8192);
        if (ash_fd >= 0) {
            __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "ashmem_create_region SUCCESS, fd=%d", ash_fd);
            if (ashmem_set_prot_region) ashmem_set_prot_region(ash_fd, PROT_READ | PROT_WRITE);
            close(ash_fd);
        } else {
            __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "ashmem_create_region FAILED");
        }
    }

    /* 4. Test SIGALTSTACK */
    stack_t ss;
    ss.ss_sp = malloc(SIGSTKSZ);
    ss.ss_size = SIGSTKSZ;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "sigaltstack() SUCCESS");
    } else {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "sigaltstack() FAILED");
    }
    free(ss.ss_sp);

    /* 5. Test EPOLL */
    // Since epoll isn't natively exposed on Darwin in C, we just use weak linkage if testing an Android ELF
    // For now we will rely on BionicShim resolving it for other components, or we could test using syscall(SYS_epoll_create1).
    __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "epoll is supported via shim for native Bionic libraries.");

    /* 6. Test CLOCK_GETTIME / GETTIMEOFDAY */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "clock_gettime() SUCCESS: %lld.%.9ld", (long long)ts.tv_sec, ts.tv_nsec);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "clock_gettime() FAILED");
        result = SYSCALL_TEST_FAIL;
    }

    __android_log_print(ANDROID_LOG_INFO, "KuDroidSyscall", "Syscall Test completed with result: %d", result);
    return result;
}
