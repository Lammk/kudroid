/* Freestanding verification of the guest vararg trampolines under real arm64.
 *
 * A guest .so is built for Linux AAPCS64: the first eight integer varargs arrive in
 * x0-x7, the first eight floating-point varargs in v0-v7, the rest on the stack. Apple's
 * arm64 ABI passes every variadic argument on the stack instead, so forwarding a guest's
 * variadic call to the host implementation makes it read the wrong place. That is not a
 * crash — it prints plausible-looking numbers, which is why snprintf reported a check
 * counter of 1839197616 before this was fixed.
 *
 * Nothing on an x86-64 host can catch that: the register files and the convention are
 * both different. So this cross-compiles the real trampoline plus the real formatter and
 * calls them the way a guest would — a genuine variadic call, letting the compiler place
 * the arguments per AAPCS64 — then checks the text that comes out.
 *
 * Freestanding because the toolchain has an aarch64 compiler but no aarch64 sysroot; the
 * few libc functions the formatter needs are provided here. Exit status is the number of
 * failed checks. */

typedef unsigned long long u64;
typedef unsigned long size_t;

/* The trampolines, as the shim table exposes them. Declared variadic so the compiler
 * emits a normal AAPCS64 variadic call — exactly what guest code does. */
extern int kudroid_snprintf_trampoline(char* buf, size_t size, const char* fmt, ...);
extern int kudroid_sprintf_trampoline(char* buf, const char* fmt, ...);
/* The _FORTIFY_SOURCE forms, which is what a release-built guest actually calls. More
 * fixed arguments before the varargs, so an off-by-one in the starting register index
 * shows up here and not in the plain versions. */
extern int kudroid_snprintf_chk_trampoline(char* buf, size_t maxlen, int flag, size_t slen,
                                           const char* fmt, ...);
extern int kudroid_sprintf_chk_trampoline(char* buf, int flag, size_t slen,
                                          const char* fmt, ...);

/* The va_list path, declared the way a guest declares __android_log_vprint.
 *
 * This is the half of the ABI split that a trampoline cannot cover. AAPCS64's va_list is a
 * 32-byte composite, and composites are passed BY REFERENCE — so the compiler hands over a
 * POINTER to it. Apple's va_list is a plain char* passed BY VALUE, so the same register
 * means two different things on the two platforms, and forwarding a guest's va_list to the
 * host's vsnprintf makes the host read the struct's own bytes as a stack cursor.
 *
 * Declaring the parameter as va_list HERE is what makes this test worth anything: the
 * compiler then emits the genuine by-reference call that guest code emits. The C++ side
 * receives it as a const void* and rebuilds the register capture from it.
 *
 * On device the failure printed "Unable to locate asset: H,\x98B\x01" — the low five bytes
 * of a stack pointer where a filename should have been. The asset existed; its name was
 * never formatted. */
extern unsigned long kudroid_format_guest_va_list(char* out, size_t size, const char* fmt,
                                                  __builtin_va_list ap);

/* The log trampoline lives in the same assembly file and so needs its handler resolved at
 * link time, but the real one is in SyscallShim.cpp and pulls in the whole shim — mutexes,
 * files, the crash buffer — none of which can be built freestanding. Stubbed because this
 * test is about argument placement, which the snprintf path exercises identically: both
 * trampolines are the same macro expansion, and both take varargs from the fourth integer
 * register. Same reasoning for the assert handler. */
int kudroid_android_log_print_from_registers(const u64* frame) { (void)frame; return 0; }
int kudroid_log_assert_from_registers(const u64* frame) { (void)frame; return 0; }

/* ── libc pieces the C++ formatter references ─────────────────────────────────────── */

static long sys_write(int fd, const void* buf, unsigned long n) {
    register long x8 __asm__("x8") = 64;      /* __NR_write */
    register long x0 __asm__("x0") = fd;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = (long)n;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}
static void sys_exit(int code) {
    register long x8 __asm__("x8") = 93;      /* __NR_exit_group */
    register long x0 __asm__("x0") = code;
    __asm__ volatile("svc #0" :: "r"(x0), "r"(x8));
    __builtin_unreachable();
}
static void puts_(const char* s) {
    unsigned long n = 0;
    while (s[n]) ++n;
    sys_write(1, s, n);
}

static int failures = 0;
static int checks = 0;

static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void Check(int ok, const char* what) {
    ++checks;
    puts_(ok ? "  OK   " : "  FAIL ");
    puts_(what);
    puts_("\n");
    if (!ok) ++failures;
}

/* Report the text produced, so a failure says what came out rather than only that
 * something differed. This is the whole diagnostic value: the bug's signature is
 * plausible garbage, and seeing it is what identifies it. */
static void CheckText(const char* got, const char* want, const char* what) {
    ++checks;
    if (streq(got, want)) {
        puts_("  OK   ");
        puts_(what);
        puts_("\n");
        return;
    }
    ++failures;
    puts_("  FAIL ");
    puts_(what);
    puts_("\n         want: \"");
    puts_(want);
    puts_("\"\n         got:  \"");
    puts_(got);
    puts_("\"\n");
}

/* ── the va_list forwarding path ──────────────────────────────────────────────────── */

/* Stands in for a guest's __android_log_vprint caller: run va_start, hand the list on.
 * This is the exact shape of the code that produced the corrupted asset name. */
static int guest_vlog(char* out, size_t size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const int n = (int)kudroid_format_guest_va_list(out, size, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

/* A guest that consumes some arguments before forwarding the rest. The va_list then carries
 * non-initial offsets, and a converter that assumed "nothing consumed yet" would restart
 * from the first register and print the already-read values a second time. */
static int guest_vlog_after_two(char* out, size_t size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    (void)__builtin_va_arg(ap, int);
    (void)__builtin_va_arg(ap, int);
    const int n = (int)kudroid_format_guest_va_list(out, size, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

/* The same for the floating-point file, which is tracked by a SEPARATE offset. Consuming a
 * double must advance vr_offs and leave gr_offs alone; one shared counter gets this wrong
 * in a way that only shows when both files are in play. */
static int guest_vlog_after_double(char* out, size_t size, const char* fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    (void)__builtin_va_arg(ap, double);
    const int n = (int)kudroid_format_guest_va_list(out, size, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

static void RunVaListChecks(char* buf, size_t size) {
    puts_("-- guest va_list, passed by reference --\n");

    guest_vlog(buf, size, "%d", 42);
    CheckText(buf, "42", "a va_list yields the register value, not its own bytes");

    /* The device signature: a %s whose argument is a real string. Reading the va_list
     * struct as a cursor printed the low bytes of its `stack` field instead. */
    guest_vlog(buf, size, "%s", "assets/gui/hud.png");
    CheckText(buf, "assets/gui/hud.png", "a va_list %s is the string, not a stack address");

    guest_vlog(buf, size, "Unable to locate asset: %s", "sounds/click.fsb");
    CheckText(buf, "Unable to locate asset: sounds/click.fsb",
              "the exact log line that came out corrupted on device");

    guest_vlog(buf, size, "%d %d %d %d %d", 1, 2, 3, 4, 5);
    CheckText(buf, "1 2 3 4 5", "five integers through a va_list");

    /* Past the register file: the va_list's `stack` field takes over, and it is the field a
     * misread starts from — so this distinguishes a working converter from a lucky one. */
    guest_vlog(buf, size, "%d %d %d %d %d %d %d %d", 1, 2, 3, 4, 5, 6, 7, 8);
    CheckText(buf, "1 2 3 4 5 6 7 8", "a va_list overflows to the stack correctly");

    guest_vlog(buf, size, "%d %.1f", 7, 2.5);
    CheckText(buf, "7 2.5", "a va_list keeps the two register files separate");

    guest_vlog(buf, size, "%.2f %d %.2f %d", 1.5, 1, 2.5, 2);
    CheckText(buf, "1.50 1 2.50 2", "interleaved doubles and ints through a va_list");

    guest_vlog(buf, size, "%s", (const char*)0);
    CheckText(buf, "<null>", "a null %s through a va_list is reported, not dereferenced");

    puts_("-- a partly consumed va_list --\n");

    guest_vlog_after_two(buf, size, "%d", 1, 2, 3);
    CheckText(buf, "3", "forwarding resumes where the guest stopped reading");

    guest_vlog_after_two(buf, size, "%d %d %d", 1, 2, 3, 4, 5);
    CheckText(buf, "3 4 5", "the remaining integers keep their order");

    guest_vlog_after_double(buf, size, "%d", 1.5, 9);
    CheckText(buf, "9", "consuming a double does not consume an integer");

    guest_vlog_after_double(buf, size, "%.1f", 1.5, 2.5);
    CheckText(buf, "2.5", "the float offset advances independently");

    /* A fabricated va_list must be refused rather than followed. A guest that passes a
     * stale one would otherwise have the formatter read from whatever gr_top held.
     *
     * Called through a pointer-typed view of the same symbol: a va_list is a composite and
     * so cannot be cast from 0, but the ABI passes it as a pointer, which is precisely the
     * property under test. */
    typedef unsigned long (*format_by_pointer)(char*, size_t, const char*, const void*);
    const format_by_pointer format_ptr =
        (format_by_pointer)(void*)&kudroid_format_guest_va_list;
    unsigned long n = format_ptr(buf, size, "%d", (const void*)0);
    Check(buf[0] == '<', "a null va_list is reported, not followed");
    Check(n > 0, "a rejected va_list still reports a length");
}

int main(void) {
    char buf[512];

    puts_("-- integer varargs in x0-x7 --\n");

    /* One integer: the simplest case, and the one that failed loudest — a count printed
     * as a stack word instead of the value. */
    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d", 42);
    CheckText(buf, "42", "a single %d reads the register, not the stack");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d checks, %d failed", 41, 0);
    CheckText(buf, "41 checks, 0 failed", "two integers keep their order");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d", -7);
    CheckText(buf, "-7", "a negative integer keeps its sign");

    /* Exactly five integer varargs after buf/size/format fills x3-x7, the last register.
     * Off-by-one in the starting index shows up here and nowhere earlier. */
    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d %d %d %d %d", 1, 2, 3, 4, 5);
    CheckText(buf, "1 2 3 4 5", "five integers fill the register file exactly");

    puts_("-- overflow onto the stack --\n");

    /* Six varargs: the sixth has no register left and must come from the caller's stack.
     * This is the boundary the trampoline's saved SP exists for. */
    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d %d %d %d %d %d", 1, 2, 3, 4, 5, 6);
    CheckText(buf, "1 2 3 4 5 6", "the sixth integer comes from the overflow area");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d %d %d %d %d %d %d %d",
                                1, 2, 3, 4, 5, 6, 7, 8);
    CheckText(buf, "1 2 3 4 5 6 7 8", "eight integers span registers and stack");

    puts_("-- strings and pointers --\n");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%s", "hello");
    CheckText(buf, "hello", "a string argument is dereferenced");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "[%s] %s", "tag", "message");
    CheckText(buf, "[tag] message", "two strings keep their order");

    /* A null %s must not fault: guest code passes null pointers, and a formatter that
     * dereferences one takes the process down with no log at all. */
    kudroid_snprintf_trampoline(buf, sizeof(buf), "%s", (const char*)0);
    CheckText(buf, "<null>", "a null string is reported, not dereferenced");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%x", 255);
    CheckText(buf, "ff", "%x formats hexadecimal");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "0x%lx", (unsigned long)0xEF53);
    CheckText(buf, "0xef53", "%lx handles the length modifier");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%u", 4000000000u);
    CheckText(buf, "4000000000", "%u is unsigned, not a negative int");

    puts_("-- mixed integer and floating point --\n");

    /* The case a single shared argument counter gets wrong. Integers come from x-
     * registers and doubles from v-registers, two independent files, so consuming a
     * double must not advance the integer index. A game logging "frame %d at %f" hits
     * this on every frame. */
    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d %.1f", 7, 2.5);
    CheckText(buf, "7 2.5", "an int and a double come from separate register files");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%.1f %d", 2.5, 7);
    CheckText(buf, "2.5 7", "the same, with the double first");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%d %.2f %d %.2f", 1, 1.5, 2, 2.5);
    CheckText(buf, "1 1.50 2 2.50", "interleaved ints and doubles stay paired");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "%.3f", 1.0 / 3.0);
    CheckText(buf, "0.333", "precision is honoured");

    puts_("-- literals and escapes --\n");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "no conversions here");
    CheckText(buf, "no conversions here", "a format with no conversions is copied");

    kudroid_snprintf_trampoline(buf, sizeof(buf), "100%%");
    CheckText(buf, "100%", "%% is a literal percent");

    /* A trailing '%' is malformed but must terminate rather than run past the string.
     * Guest format strings are guest data; a scan off the end is a crash in a log call. */
    kudroid_snprintf_trampoline(buf, sizeof(buf), "trailing %");
    Check(buf[0] == 't', "a trailing %% does not run off the format string");

    puts_("-- return value and truncation --\n");

    /* snprintf returns the length it WOULD have written. Callers size a second buffer
     * from it, so returning the truncated length makes them allocate too little. */
    int n = kudroid_snprintf_trampoline(buf, sizeof(buf), "%d", 12345);
    Check(n == 5, "the return value is the formatted length");

    char small[8];
    n = kudroid_snprintf_trampoline(small, sizeof(small), "%s", "0123456789");
    Check(n == 10, "the return value is the untruncated length");
    Check(small[7] == '\0', "the buffer is still terminated when truncated");
    CheckText(small, "0123456", "truncation keeps the leading characters");

    /* size 0 must write nothing at all — not even a terminator. */
    char guard[4];
    guard[0] = 'Z';
    n = kudroid_snprintf_trampoline(guard, 0, "%d", 1);
    Check(guard[0] == 'Z', "size 0 writes nothing");
    Check(n == 1, "size 0 still reports the length");

    puts_("-- sprintf --\n");

    kudroid_sprintf_trampoline(buf, "%d-%s-%.1f", 5, "x", 1.5);
    CheckText(buf, "5-x-1.5", "sprintf takes its format one register earlier");

    puts_("-- fortified forms --\n");

    /* __snprintf_chk(buf, maxlen, flag, slen, fmt, ...): four fixed arguments, so varargs
     * begin at x5 and only three registers are left before the overflow area. A guest
     * built with _FORTIFY_SOURCE — which release builds are — calls this rather than
     * snprintf, so getting the index wrong here breaks the common case while the plain
     * version above still passes. */
    kudroid_snprintf_chk_trampoline(buf, sizeof(buf), 0, sizeof(buf), "%d", 42);
    CheckText(buf, "42", "__snprintf_chk finds its first vararg at x5");

    kudroid_snprintf_chk_trampoline(buf, sizeof(buf), 0, sizeof(buf), "%d %s %d", 1, "mid", 2);
    CheckText(buf, "1 mid 2", "__snprintf_chk keeps three varargs in order");

    /* Three varargs fill x5-x7; the fourth must come from the stack. This is the boundary
     * the fortified form reaches sooner than the plain one. */
    kudroid_snprintf_chk_trampoline(buf, sizeof(buf), 0, sizeof(buf), "%d %d %d %d",
                                    1, 2, 3, 4);
    CheckText(buf, "1 2 3 4", "__snprintf_chk overflows to the stack after x7");

    kudroid_snprintf_chk_trampoline(buf, sizeof(buf), 0, sizeof(buf), "%d %.1f", 7, 2.5);
    CheckText(buf, "7 2.5", "__snprintf_chk mixes int and double correctly");

    /* maxlen is honoured, not the compiler's slen. */
    char small2[8];
    int nchk = kudroid_snprintf_chk_trampoline(small2, sizeof(small2), 0, sizeof(small2),
                                               "%s", "0123456789");
    Check(nchk == 10, "__snprintf_chk reports the untruncated length");
    CheckText(small2, "0123456", "__snprintf_chk truncates at maxlen");

    /* __sprintf_chk(buf, flag, slen, fmt, ...): three fixed arguments, varargs from x4. */
    kudroid_sprintf_chk_trampoline(buf, 0, sizeof(buf), "%d-%s", 9, "y");
    CheckText(buf, "9-y", "__sprintf_chk finds its first vararg at x4");

    RunVaListChecks(buf, sizeof(buf));

    puts_("\n");
    if (failures == 0) {
        puts_("=== vararg trampoline PASSED ===\n");
        sys_exit(0);
    }
    puts_("=== vararg trampoline FAILED ===\n");
    sys_exit(failures);
    return failures;
}
