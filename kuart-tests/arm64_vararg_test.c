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

/* The log trampoline lives in the same assembly file and so needs its handler resolved at
 * link time, but the real one is in SyscallShim.cpp and pulls in the whole shim — mutexes,
 * files, the crash buffer — none of which can be built freestanding. Stubbed because this
 * test is about argument placement, which the snprintf path exercises identically: both
 * trampolines are the same macro expansion, and both take varargs from the fourth integer
 * register. */
int kudroid_android_log_print_from_registers(const u64* frame) { (void)frame; return 0; }

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

    puts_("\n");
    if (failures == 0) {
        puts_("=== vararg trampoline PASSED ===\n");
        sys_exit(0);
    }
    puts_("=== vararg trampoline FAILED ===\n");
    sys_exit(failures);
    return failures;
}
