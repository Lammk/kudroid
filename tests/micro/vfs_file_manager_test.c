typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef int mode_t;
typedef long off_t;
#define NULL ((void*)0)

// Android Log API
extern int __android_log_print(int priority, const char* tag, const char* fmt, ...);
#define LOG_TAG "VFSManagerTest"
#define LOGI(...) __android_log_print(3, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(6, LOG_TAG, __VA_ARGS__)

// POSIX File APIs (routed via SyscallShim & Bionic)
struct dirent {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[256];
};
typedef void DIR;

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    mode_t   st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    off_t    st_size;
    long     st_blksize;
    long     st_blocks;
    long     st_atime;
    long     st_mtime;
    long     st_ctime;
};

extern DIR* opendir(const char* name);
extern struct dirent* readdir(DIR* dirp);
extern int closedir(DIR* dirp);

extern int open(const char* pathname, int flags, ...);
extern ssize_t read(int fd, void* buf, size_t count);
extern ssize_t write(int fd, const void* buf, size_t count);
extern int close(int fd);
extern int stat(const char* pathname, struct stat* statbuf);
extern int mkdir(const char* pathname, mode_t mode);
extern int rmdir(const char* pathname);
extern int unlink(const char* pathname);
extern int rename(const char* oldpath, const char* newpath);

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR   02
#define O_CREAT  0100
#define O_TRUNC  01000

// C runtime
extern size_t strlen(const char* s);
extern int strcmp(const char* s1, const char* s2);
extern int strncmp(const char* s1, const char* s2, size_t n);
extern void* memset(void* s, int c, size_t n);

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            LOGI("  ✔ [PASS] %s", msg); \
            g_passCount++; \
        } else { \
            LOGE("  ❌ [FAIL] %s (Line %d)", msg, __LINE__); \
            g_failCount++; \
        } \
    } while(0)

int kudroid_test_main(void) {
    g_passCount = 0;
    g_failCount = 0;

    LOGI("=================================================");
    LOGI("📁 [KUDROID VFS FILE MANAGER & ZARCHIVER TEST]");
    LOGI("=================================================");

    // ──────────────────────────────────────────────
    // 1. STORAGE DIRECTORY SCAN TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 1] Scan /sdcard Root Directory...");
    DIR* sdcardDir = opendir("/sdcard");
    TEST_ASSERT(sdcardDir != NULL, "opendir(/sdcard) succeeds");
    
    int foundDownload = 0;
    int foundDocuments = 0;
    int foundAndroid = 0;
    if (sdcardDir) {
        struct dirent* entry;
        while ((entry = readdir(sdcardDir)) != NULL) {
            if (strcmp(entry->d_name, "Download") == 0) foundDownload = 1;
            if (strcmp(entry->d_name, "Documents") == 0) foundDocuments = 1;
            if (strcmp(entry->d_name, "Android") == 0) foundAndroid = 1;
        }
        closedir(sdcardDir);
    }
    TEST_ASSERT(foundDownload == 1, "Found /sdcard/Download directory");
    TEST_ASSERT(foundDocuments == 1, "Found /sdcard/Documents directory");
    TEST_ASSERT(foundAndroid == 1, "Found /sdcard/Android directory");

    // ──────────────────────────────────────────────
    // 2. EMULATED/0 ALIAS SCAN TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 2] Scan /storage/emulated/0 Symlink/Alias...");
    DIR* emulatedDir = opendir("/storage/emulated/0");
    TEST_ASSERT(emulatedDir != NULL, "opendir(/storage/emulated/0) succeeds");
    if (emulatedDir) closedir(emulatedDir);

    // ──────────────────────────────────────────────
    // 3. FILE CREATION, WRITE & STAT TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 3] Create & Write Archive File in /sdcard/Download...");
    const char* filePath = "/sdcard/Download/kudroid_test_archive.zip";
    int fd = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "open(/sdcard/Download/...zip, O_CREAT) succeeds");
    
    const char* zipDummyHeader = "PK\x03\x04\x14\x00\x00\x00\x08\x00KuDroidFakeZipDataPayload";
    size_t headerLen = strlen(zipDummyHeader);
    ssize_t written = write(fd, zipDummyHeader, headerLen);
    TEST_ASSERT(written == (ssize_t)headerLen, "write dummy zip header matches length");
    close(fd);

    struct stat st;
    memset(&st, 0, sizeof(st));
    int statRes = stat(filePath, &st);
    TEST_ASSERT(statRes == 0, "stat(/sdcard/Download/...zip) succeeds");
    TEST_ASSERT(st.st_size == (off_t)headerLen, "stat reports exact file size");

    // ──────────────────────────────────────────────
    // 4. READ VERIFICATION TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 4] Read & Verify File Content...");
    int readFd = open(filePath, O_RDONLY);
    TEST_ASSERT(readFd >= 0, "open(/sdcard/Download/...zip, O_RDONLY) succeeds");
    char readBuf[128];
    memset(readBuf, 0, sizeof(readBuf));
    ssize_t bytesRead = read(readFd, readBuf, sizeof(readBuf) - 1);
    TEST_ASSERT(bytesRead == (ssize_t)headerLen, "read bytes count matches");
    TEST_ASSERT(strcmp(readBuf, zipDummyHeader) == 0, "read content matches payload exactly");
    close(readFd);

    // ──────────────────────────────────────────────
    // 5. DIRECTORY CREATION & RENAME TEST
    // ──────────────────────────────────────────────
    LOGI("🧪 [TEST 5] Subfolder Creation, Move & Cleanup...");
    const char* subFolder = "/sdcard/Download/extracted_archive";
    int mkdirRes = mkdir(subFolder, 0755);
    TEST_ASSERT(mkdirRes == 0, "mkdir(/sdcard/Download/extracted_archive) succeeds");

    const char* destPath = "/sdcard/Download/extracted_archive/moved_archive.zip";
    int renameRes = rename(filePath, destPath);
    TEST_ASSERT(renameRes == 0, "rename (move) file into subfolder succeeds");

    int oldStat = stat(filePath, &st);
    TEST_ASSERT(oldStat != 0, "old file path no longer exists (moved)");

    int newStat = stat(destPath, &st);
    TEST_ASSERT(newStat == 0, "new file path exists and is accessible");

    // Dọn dẹp
    unlink(destPath);
    rmdir(subFolder);

    LOGI("=================================================");
    LOGI("📊 VFS FILE MANAGER AUDIT: %d PASSED, %d FAILED (TOTAL %d)",
         g_passCount, g_failCount, g_passCount + g_failCount);
    LOGI("=================================================");

    return (g_failCount == 0) ? 0 : 1;
}
