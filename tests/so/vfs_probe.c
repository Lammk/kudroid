// On-device verification for the four filesystem fixes, as a guest .so.
//
// Built as an Android arm64 shared object and run through `kdb` with
//
//     so tests/so/vfs_probe.so
//
// which uploads it, loads it with LibraryManager (so every libc call below binds to
// KuDroid's shim table exactly as a real guest's would) and prints what this returns.
//
// The host test tests/test_vfs.cpp already covers the remapper's logic. What it cannot
// cover is the part that only exists on a device: whether the shim table actually routes
// a guest's calls, whether the struct layouts KuDroid declares match what guest code
// compiled against bionic headers expects, and whether the container path the remapper
// derives at runtime is the right one. Those are exactly the four things below.
//
// Written freestanding (-nostdlib) so the object has no DT_NEEDED entry: a dependency on
// libc.so.6 would make LibraryManager go looking for a library that does not exist here,
// and the failure would be about loading rather than about the filesystem.

// ── the bionic-side declarations a real guest would get from its NDK headers ─────────
// Deliberately declared here rather than included: the point is to call KuDroid with the
// types and flag values guest code uses, and a mismatch in either is the bug being
// looked for.

typedef unsigned long size_t;
typedef long ssize_t;
typedef unsigned int mode_t;
typedef struct _FILE FILE;
typedef struct _DIR DIR;

#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT  0x40   // Linux value; the shim translates it for Darwin
#define O_TRUNC  0x200
#define AT_FDCWD (-100) // Linux value; the shim translates it too
#define F_OK 0

// bionic's struct statfs for arm64. If KuDroid's idea of this layout is wrong, the
// fields read below land on the wrong words.
struct bionic_statfs {
    unsigned long f_type;
    unsigned long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    struct { int val[2]; } f_fsid;
    unsigned long f_namelen;
    unsigned long f_frsize;
    unsigned long f_flags;
    unsigned long f_spare[4];
};

// bionic's struct dirent for arm64. d_name sits at offset 19; Darwin's sits elsewhere,
// so a readdir64 that was not shimmed hands back a name read out of other fields.
struct bionic_dirent {
    unsigned long long d_ino;
    long long          d_off;
    unsigned short     d_reclen;
    unsigned char      d_type;
    char               d_name[256];
};

extern int   __android_log_print(int prio, const char* tag, const char* fmt, ...);
extern int   snprintf(char* buf, size_t n, const char* fmt, ...);
extern FILE* fopen(const char* path, const char* mode);
extern int   fclose(FILE* f);
extern int   fputs(const char* s, FILE* f);
extern char* fgets(char* s, int n, FILE* f);
extern int   open(const char* path, int flags, ...);
extern int   openat(int dirfd, const char* path, int flags, ...);
extern int   close(int fd);
extern int   access(const char* path, int mode);
extern int   mkdir(const char* path, mode_t mode);
extern int   unlink(const char* path);
extern int   statfs(const char* path, struct bionic_statfs* buf);
extern DIR*  opendir(const char* path);
extern struct bionic_dirent* readdir64(DIR* d);
extern int   closedir(DIR* d);
extern char* realpath(const char* path, char* resolved);
extern size_t strlen(const char* s);
extern int   strcmp(const char* a, const char* b);

// ── log buffer ───────────────────────────────────────────────────────────────────────
// Returned to kudroid_run_so_test, which prints it and forwards it to kdb. Also mirrored
// through __android_log_print so the lines survive even if this crashes before
// returning — which is the failure mode that matters most when probing a new shim.

static char g_log[16384];
static size_t g_len = 0;
static int g_failures = 0;
static int g_checks = 0;

static void emit(const char* line) {
    __android_log_print(4, "kudroid_vfs_probe", "%s", line);
    size_t n = strlen(line);
    if (g_len + n + 2 >= sizeof(g_log)) return;
    for (size_t i = 0; i < n; ++i) g_log[g_len++] = line[i];
    g_log[g_len++] = '\n';
    g_log[g_len] = '\0';
}

static void check(int ok, const char* what) {
    char line[512];
    ++g_checks;
    if (!ok) ++g_failures;
    snprintf(line, sizeof(line), "  %s %s", ok ? "OK  " : "FAIL", what);
    emit(line);
}

static void note(const char* fmt, const char* a) {
    char line[512];
    snprintf(line, sizeof(line), fmt, a);
    emit(line);
}

// Write `text` to `path`, then say whether it could be read back. The whole write path a
// guest uses, in one call.
static int writeThenRead(const char* path, const char* text) {
    FILE* out = fopen(path, "w");
    if (!out) return 0;
    fputs(text, out);
    fclose(out);

    char buf[128];
    buf[0] = '\0';
    FILE* in = fopen(path, "r");
    if (!in) return 0;
    fgets(buf, sizeof(buf), in);
    fclose(in);
    return strcmp(buf, text) == 0;
}

// True when `path` starts with `prefix` at a component boundary.
static int hasPrefix(const char* path, const char* prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (path[i] != prefix[i]) return 0;
        ++i;
    }
    return path[i] == '\0' || path[i] == '/';
}

// ── 1. containment ───────────────────────────────────────────────────────────────────
// The remapper joins strings and the kernel resolves ".." after the join, so a guest
// path containing ".." used to walk out of android_root and reach the real iOS
// container. Verified on the device because the container layout is what makes it
// reachable, and because the root is only known at runtime.
static void testContainment(char* root, size_t rootSize) {
    emit("-- containment --");

    // Discover android_root the way only a guest can: ask what a known Android
    // directory resolves to. /data/data exists because initialize() creates it.
    char resolved[1024];
    resolved[0] = '\0';
    if (!realpath("/data/data", resolved)) {
        emit("  FAIL realpath(\"/data/data\") failed; cannot locate android_root");
        ++g_failures;
        ++g_checks;
        return;
    }
    // Strip the trailing "/data/data" to leave the root.
    size_t n = strlen(resolved);
    const size_t tail = 10; // strlen("/data/data")
    if (n > tail) n -= tail;
    if (n >= rootSize) n = rootSize - 1;
    for (size_t i = 0; i < n; ++i) root[i] = resolved[i];
    root[n] = '\0';
    note("       android_root: %s", root);

    check(writeThenRead("/data/data/com.kudroid.probe/files/a.txt", "VFS_OK"),
          "an app data path is writable and reads back");
    check(writeThenRead("/sdcard/Download/probe.txt", "SD_OK"),
          "an sdcard path is writable and reads back");

    // Each of these escapes reached outside the root before normalisation was added.
    // Creating the file first is what makes realpath able to report where it landed;
    // that it can be created at all is not the question — where it lands is.
    static const char* escapes[3] = {
        "/data/data/../../../../../../kudroid_escape_a.txt",
        "/sdcard/../../../kudroid_escape_b.txt",
        "/system/../../../../../../../../kudroid_escape_c.txt",
    };
    for (int i = 0; i < 3; ++i) {
        char where[1024];
        where[0] = '\0';
        int created = writeThenRead(escapes[i], "X");
        int contained = 0;
        if (created && realpath(escapes[i], where)) {
            contained = hasPrefix(where, root);
        }
        char line[1200];
        snprintf(line, sizeof(line), "contained: %s -> %s", escapes[i],
                 where[0] ? where : "<unresolved>");
        check(created && contained, line);
        unlink(escapes[i]);
    }

    unlink("/data/data/com.kudroid.probe/files/a.txt");
    unlink("/sdcard/Download/probe.txt");
}

// ── 2. statfs layout ─────────────────────────────────────────────────────────────────
// Darwin's struct statfs is a different shape and size from bionic's, so forwarding a
// guest buffer to the host call has it read f_bsize out of what is actually f_type. Apps
// check free space before writing anything large, and a garbage answer reads as "disk
// full" or as plenty of room followed by a failed write.
static void testStatfs(void) {
    emit("-- statfs --");

    struct bionic_statfs st;
    for (unsigned i = 0; i < sizeof(st); ++i) ((char*)&st)[i] = 0;

    int rc = statfs("/sdcard", &st);
    check(rc == 0, "statfs(\"/sdcard\") succeeds");
    if (rc != 0) return;

    char line[512];
    snprintf(line, sizeof(line),
             "       f_type=0x%lx f_bsize=%lu f_blocks=%lu f_bavail=%lu f_namelen=%lu",
             st.f_type, st.f_bsize, st.f_blocks, st.f_bavail, st.f_namelen);
    emit(line);

    // Each of these is a field at a specific offset. A wrong layout fails at least one.
    check(st.f_type == 0xEF53UL, "f_type is EXT4_SUPER_MAGIC, so apps see a real fs");
    check(st.f_bsize >= 512UL && st.f_bsize <= (1UL << 20),
          "f_bsize is a plausible block size, not a misread field");
    check(st.f_blocks > 0UL, "f_blocks is non-zero");
    check(st.f_bavail <= st.f_blocks, "f_bavail does not exceed f_blocks");
    check(st.f_namelen >= 8UL && st.f_namelen <= 4096UL, "f_namelen is plausible");
}

// ── 3. readdir64 dirent layout ───────────────────────────────────────────────────────
// readdir64 is a distinct symbol in bionic and the one 64-bit guest code links against.
// Unshimmed it resolved through dlsym to Darwin's readdir, whose struct dirent puts
// d_name at a different offset — so the guest reads filenames out of the middle of other
// fields and sees garbage rather than an error.
static void testReaddir64(void) {
    emit("-- readdir64 --");

    mkdir("/sdcard/kudroid_probe_dir", 0755);
    // A name long enough that a wrong d_name offset cannot coincidentally match, and
    // distinctive enough to recognise in a log.
    const char* wanted = "kudroid_readdir_marker_file.txt";
    check(writeThenRead("/sdcard/kudroid_probe_dir/kudroid_readdir_marker_file.txt", "M"),
          "a file with a known name exists in the directory");

    DIR* dir = opendir("/sdcard/kudroid_probe_dir");
    check(dir != 0, "opendir succeeds on a remapped directory");
    int found = 0;
    int entries = 0;
    if (dir) {
        struct bionic_dirent* entry;
        while ((entry = readdir64(dir)) != 0) {
            ++entries;
            if (strcmp(entry->d_name, wanted) == 0) found = 1;
            if (entries == 1) note("       first entry d_name=\"%s\"", entry->d_name);
            if (entries > 64) break; // a garbage layout can loop on rubbish
        }
        closedir(dir);
    }
    check(found, "readdir64 reports the file under its real name (d_name offset correct)");

    unlink("/sdcard/kudroid_probe_dir/kudroid_readdir_marker_file.txt");
}

// ── 4. openat with a real directory fd ───────────────────────────────────────────────
// A relative path is resolved by the kernel against dirfd, so remapping it is wrong
// twice: the remapper treats it as a stray relative path and roots it under
// data/local/tmp, and the kernel then resolves that absolute result against dirfd
// anyway. Before the fix this could only ever fail.
static void testOpenat(void) {
    emit("-- openat --");

    mkdir("/sdcard/kudroid_probe_at", 0755);
    check(writeThenRead("/sdcard/kudroid_probe_at/inner.txt", "AT_OK"),
          "a file exists inside the directory to be opened");

    int dirfd = open("/sdcard/kudroid_probe_at", O_RDONLY);
    check(dirfd >= 0, "the directory itself can be opened to get a dirfd");

    if (dirfd >= 0) {
        // The case that was broken: relative path, real dirfd.
        int fd = openat(dirfd, "inner.txt", O_RDONLY);
        check(fd >= 0, "openat(dirfd, \"inner.txt\") resolves against the dirfd");
        if (fd >= 0) close(fd);
        close(dirfd);
    }

    // AT_FDCWD with an absolute path must still be remapped — the other half of the
    // condition, which would break if the fix had simply stopped remapping everything.
    int fd2 = openat(AT_FDCWD, "/sdcard/kudroid_probe_at/inner.txt", O_RDONLY);
    check(fd2 >= 0, "openat(AT_FDCWD, absolute) is still remapped");
    if (fd2 >= 0) close(fd2);

    unlink("/sdcard/kudroid_probe_at/inner.txt");
}

// ── entry point ──────────────────────────────────────────────────────────────────────

const char* kudroid_test_main(void) {
    g_len = 0;
    g_log[0] = '\0';
    g_failures = 0;
    g_checks = 0;

    emit("=== KuDroid VFS on-device probe ===");

    char root[1024];
    root[0] = '\0';
    testContainment(root, sizeof(root));
    testStatfs();
    testReaddir64();
    testOpenat();

    char line[256];
    snprintf(line, sizeof(line), "=== %d checks, %d failed ===", g_checks, g_failures);
    emit(line);
    emit(g_failures == 0 ? "=== VFS PROBE PASSED ===" : "=== VFS PROBE FAILED ===");
    return g_log;
}
