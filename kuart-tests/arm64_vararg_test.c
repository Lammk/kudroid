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

/* The scanf trampolines. Declared variadic so the compiler emits the genuine AAPCS64 call:
 * output pointers in x2-x7 and the rest on the stack.
 *
 * This is the same ABI split with the consequences reversed. printf read the wrong place
 * and produced rubbish; scanf STORES to the wrong place. On device Minecraft's UUID parse
 * passed eleven output pointers, the host took five of them from stack words that were
 * never pointers, and one of those held a jobject — reported by the next JNI call as
 * clazz=0xf8ec9809, a value no heap pointer can be. The store after that went to 0. */
extern int kudroid_sscanf_trampoline(const char* input, const char* fmt, ...);

/* The log trampoline lives in the same assembly file and so needs its handler resolved at
 * link time, but the real one is in SyscallShim.cpp and pulls in the whole shim — mutexes,
 * files, the crash buffer — none of which can be built freestanding. Stubbed because this
 * test is about argument placement, which the snprintf path exercises identically: both
 * trampolines are the same macro expansion, and both take varargs from the fourth integer
 * register. Same reasoning for the assert handler. */
int kudroid_android_log_print_from_registers(const u64* frame) { (void)frame; return 0; }
int kudroid_log_assert_from_registers(const u64* frame) { (void)frame; return 0; }
/* fscanf's character source needs <cstdio>, which GuestVarargs.cpp deliberately avoids, so
 * its handler lives in SyscallShim.cpp and cannot be linked freestanding. The string source
 * exercises the identical argument placement — same trampoline macro, same starting
 * register index. */
int kudroid_fscanf_from_registers(const u64* frame) { (void)frame; return 0; }

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

/* ── scanning ─────────────────────────────────────────────────────────────────────── */

/* Guard bytes around every destination.
 *
 * The value being right is not the whole property: "%2hhx" must store exactly ONE byte, and
 * storing four would still produce the right value while silently overwriting the two
 * fields after it. That is worse than the crash, because nothing reports it. So each
 * destination sits inside a padded struct and the padding is checked.
 *
 * This is the check that distinguishes a correct scanner from one that merely parses. */
#define GUARD 0x5A

struct guarded_u8  { unsigned char pad_lo[8]; unsigned char v;  unsigned char pad_hi[8]; };
struct guarded_u16 { unsigned char pad_lo[8]; unsigned short v; unsigned char pad_hi[8]; };
struct guarded_u32 { unsigned char pad_lo[8]; unsigned int v;   unsigned char pad_hi[8]; };
struct guarded_u64 { unsigned char pad_lo[8]; unsigned long v;  unsigned char pad_hi[8]; };
struct guarded_f32 { unsigned char pad_lo[8]; float v;          unsigned char pad_hi[8]; };

static void fill_guard(unsigned char* p, unsigned long n) {
    for (unsigned long i = 0; i < n; ++i) p[i] = GUARD;
}
static int guard_intact(const unsigned char* p, unsigned long n) {
    for (unsigned long i = 0; i < n; ++i) if (p[i] != GUARD) return 0;
    return 1;
}
#define INIT_GUARDS(g) do { fill_guard((g).pad_lo, 8); fill_guard((g).pad_hi, 8); \
                            (g).v = 0; } while (0)
#define GUARDS_OK(g) (guard_intact((g).pad_lo, 8) && guard_intact((g).pad_hi, 8))

static void RunScanChecks(void) {
    puts_("-- scanf: output pointers in x2-x7 --\n");

    /* One conversion, the simplest case. Its output pointer is in x2. */
    {
        struct guarded_u32 a; INIT_GUARDS(a);
        const int n = kudroid_sscanf_trampoline("42", "%d", &a.v);
        Check(n == 1, "a single %d assigns one item");
        Check(a.v == 42, "the value is stored through the register pointer");
        Check(GUARDS_OK(a), "nothing was written outside the destination");
    }

    /* Six conversions fill x2-x7 exactly — the last register. */
    {
        struct guarded_u32 a, b, c, d, e, f;
        INIT_GUARDS(a); INIT_GUARDS(b); INIT_GUARDS(c);
        INIT_GUARDS(d); INIT_GUARDS(e); INIT_GUARDS(f);
        const int n = kudroid_sscanf_trampoline("1 2 3 4 5 6", "%d %d %d %d %d %d",
                                                &a.v, &b.v, &c.v, &d.v, &e.v, &f.v);
        Check(n == 6, "six integers assign six items");
        Check(a.v == 1 && b.v == 2 && c.v == 3 && d.v == 4 && e.v == 5 && f.v == 6,
              "all six values land in the right places");
        Check(GUARDS_OK(a) && GUARDS_OK(b) && GUARDS_OK(c) && GUARDS_OK(d) &&
                  GUARDS_OK(e) && GUARDS_OK(f),
              "six destinations, no overrun");
    }

    puts_("-- scanf: overflow onto the stack --\n");

    /* The seventh output pointer has no register left. This is the boundary where the host
     * implementation started reading stack words that were never pointers. */
    {
        struct guarded_u32 v[8];
        for (int i = 0; i < 8; ++i) INIT_GUARDS(v[i]);
        const int n = kudroid_sscanf_trampoline("1 2 3 4 5 6 7 8",
                                                "%d %d %d %d %d %d %d %d",
                                                &v[0].v, &v[1].v, &v[2].v, &v[3].v,
                                                &v[4].v, &v[5].v, &v[6].v, &v[7].v);
        Check(n == 8, "eight integers assign eight items");
        int values_ok = 1, guards_ok = 1;
        for (int i = 0; i < 8; ++i) {
            if (v[i].v != (unsigned)(i + 1)) values_ok = 0;
            if (!GUARDS_OK(v[i])) guards_ok = 0;
        }
        Check(values_ok, "the seventh and eighth come from the overflow area");
        Check(guards_ok, "no destination was overrun, including the stack-passed ones");
    }

    puts_("-- scanf: the UUID parse from the device log --\n");

    /* The exact format string at libminecraftpe.so+0x21803a6, with the exact destination
     * widths: one 32-bit, two 16-bit, eight 8-bit. Eleven output pointers, so five of them
     * are in the overflow area.
     *
     * Every "%2hhx" must store ONE byte. A scanner that stored four would produce the right
     * values here and destroy the guards. */
    {
        struct guarded_u32 time_low;  INIT_GUARDS(time_low);
        struct guarded_u16 time_mid;  INIT_GUARDS(time_mid);
        struct guarded_u16 time_hi;   INIT_GUARDS(time_hi);
        struct guarded_u8  n0, n1, n2, n3, n4, n5, n6, n7;
        INIT_GUARDS(n0); INIT_GUARDS(n1); INIT_GUARDS(n2); INIT_GUARDS(n3);
        INIT_GUARDS(n4); INIT_GUARDS(n5); INIT_GUARDS(n6); INIT_GUARDS(n7);

        const int n = kudroid_sscanf_trampoline(
            "6ba7b810-9dad-11d1-80b4-00c04fd430c8",
            "%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",
            &time_low.v, &time_mid.v, &time_hi.v,
            &n0.v, &n1.v, &n2.v, &n3.v, &n4.v, &n5.v, &n6.v, &n7.v);

        Check(n == 11, "all eleven fields are assigned");
        Check(time_low.v == 0x6ba7b810u, "%8x reads the 32-bit field");
        Check(time_mid.v == 0x9dadu && time_hi.v == 0x11d1u, "%4hx reads the 16-bit fields");
        Check(n0.v == 0x80 && n1.v == 0xb4, "the two %2hhx before the separator");
        Check(n2.v == 0x00 && n3.v == 0xc0 && n4.v == 0x4f && n5.v == 0xd4 &&
                  n6.v == 0x30 && n7.v == 0xc8,
              "the six %2hhx from the overflow area");
        Check(GUARDS_OK(time_low) && GUARDS_OK(time_mid) && GUARDS_OK(time_hi),
              "the integer fields did not overrun");
        Check(GUARDS_OK(n0) && GUARDS_OK(n1) && GUARDS_OK(n2) && GUARDS_OK(n3) &&
                  GUARDS_OK(n4) && GUARDS_OK(n5) && GUARDS_OK(n6) && GUARDS_OK(n7),
              "every %2hhx stored exactly one byte");
    }

    puts_("-- scanf: length modifiers store exactly their width --\n");

    /* A value with bits set beyond the destination's width, so a too-wide store is visible
     * in the guards AND a too-narrow read is visible in the value. */
    {
        struct guarded_u8 c; INIT_GUARDS(c);
        Check(kudroid_sscanf_trampoline("ff", "%hhx", &c.v) == 1, "%hhx assigns");
        Check(c.v == 0xff, "%hhx stores the byte");
        Check(GUARDS_OK(c), "%hhx wrote one byte only");
    }
    {
        struct guarded_u16 h; INIT_GUARDS(h);
        Check(kudroid_sscanf_trampoline("ffff", "%hx", &h.v) == 1, "%hx assigns");
        Check(h.v == 0xffffu, "%hx stores two bytes");
        Check(GUARDS_OK(h), "%hx wrote two bytes only");
    }
    {
        struct guarded_u64 l; INIT_GUARDS(l);
        Check(kudroid_sscanf_trampoline("123456789abcdef0", "%lx", &l.v) == 1, "%lx assigns");
        Check(l.v == 0x123456789abcdef0UL, "%lx stores all eight bytes");
        Check(GUARDS_OK(l), "%lx stayed inside eight bytes");
    }
    /* No length modifier means int, and a plain %f means FLOAT — storing a double would
     * overwrite the four bytes after it. */
    {
        struct guarded_f32 f; INIT_GUARDS(f);
        Check(kudroid_sscanf_trampoline("2.5", "%f", &f.v) == 1, "%f assigns");
        Check(f.v > 2.49f && f.v < 2.51f, "%f stores the value");
        Check(GUARDS_OK(f), "%f stored four bytes, not eight");
    }

    puts_("-- scanf: width limits and separators --\n");

    /* "%2hhx" against a longer run of digits must stop after two. Without the width the
     * first conversion would swallow the whole string. */
    {
        struct guarded_u8 a, b; INIT_GUARDS(a); INIT_GUARDS(b);
        const int n = kudroid_sscanf_trampoline("abcd", "%2hhx%2hhx", &a.v, &b.v);
        Check(n == 2, "two width-limited conversions both assign");
        Check(a.v == 0xab && b.v == 0xcd, "each took exactly two digits");
        Check(GUARDS_OK(a) && GUARDS_OK(b), "neither overran");
    }
    /* A literal that does not match stops the scan, and the count reflects what was stored
     * before it. This is what makes the '-' separators meaningful. */
    {
        struct guarded_u32 a, b; INIT_GUARDS(a); INIT_GUARDS(b);
        const int n = kudroid_sscanf_trampoline("12:34", "%d-%d", &a.v, &b.v);
        Check(n == 1, "a mismatched literal stops the scan after one assignment");
        Check(a.v == 12, "what was parsed before the mismatch is kept");
        Check(b.v == 0, "the unreached destination is untouched");
        Check(GUARDS_OK(a) && GUARDS_OK(b), "no overrun on the partial scan");
    }

    puts_("-- scanf: assignment suppression --\n");

    /* '*' parses and discards, consuming NO output pointer. Treating it as a normal
     * conversion would shift every later pointer by one — and write through the wrong one,
     * which is the same class of failure as the ABI bug itself. */
    {
        struct guarded_u32 a, b; INIT_GUARDS(a); INIT_GUARDS(b);
        const int n = kudroid_sscanf_trampoline("1 2 3", "%*d %d %d", &a.v, &b.v);
        Check(n == 2, "a suppressed conversion is not counted");
        Check(a.v == 2 && b.v == 3, "suppression consumes no output pointer");
        Check(GUARDS_OK(a) && GUARDS_OK(b), "no overrun with suppression");
    }

    puts_("-- scanf: strings, sets and characters --\n");

    {
        char s[32];
        for (int i = 0; i < 32; ++i) s[i] = GUARD;
        const int n = kudroid_sscanf_trampoline("hello world", "%s", s);
        Check(n == 1, "%s assigns");
        Check(streq(s, "hello"), "%s stops at whitespace and terminates");
        Check((unsigned char)s[6] == GUARD, "%s wrote no further than its terminator");
    }
    {
        char s[32];
        for (int i = 0; i < 32; ++i) s[i] = GUARD;
        Check(kudroid_sscanf_trampoline("abcdefgh", "%3s", s) == 1, "%3s assigns");
        Check(streq(s, "abc"), "a width-limited %s takes only three characters");
        Check((unsigned char)s[4] == GUARD, "%3s respected its width");
    }
    {
        /* %c does NOT terminate — it is a character array. Writing a terminator would
         * corrupt the byte after it. */
        struct guarded_u8 ch; INIT_GUARDS(ch);
        Check(kudroid_sscanf_trampoline("xy", "%c", (char*)&ch.v) == 1, "%c assigns");
        Check(ch.v == 'x', "%c stores one character");
        Check(GUARDS_OK(ch), "%c wrote one byte and no terminator");
    }
    {
        char s[32];
        for (int i = 0; i < 32; ++i) s[i] = GUARD;
        Check(kudroid_sscanf_trampoline("key=value", "%[^=]", s) == 1, "%[^=] assigns");
        Check(streq(s, "key"), "a negated set stops at its delimiter");
    }
    {
        char s[32];
        for (int i = 0; i < 32; ++i) s[i] = GUARD;
        Check(kudroid_sscanf_trampoline("abc123", "%[a-z]", s) == 1, "%[a-z] assigns");
        Check(streq(s, "abc"), "a set range matches only its members");
    }

    puts_("-- scanf: failure cases must not write --\n");

    /* No input at all is -1, distinct from 0. A caller deciding whether to retry reads that
     * difference. */
    {
        struct guarded_u32 a; INIT_GUARDS(a);
        const int n = kudroid_sscanf_trampoline("", "%d", &a.v);
        Check(n == -1, "empty input returns -1, not 0");
        Check(a.v == 0 && GUARDS_OK(a), "nothing was written");
    }
    {
        struct guarded_u32 a; INIT_GUARDS(a);
        const int n = kudroid_sscanf_trampoline("zzz", "%d", &a.v);
        Check(n == 0, "input that cannot match returns 0");
        Check(a.v == 0 && GUARDS_OK(a), "a matching failure writes nothing");
    }
    /* An unknown conversion must stop rather than guess: the number of output pointers it
     * would consume is unknown, so continuing would write through the wrong one. */
    {
        struct guarded_u32 a; INIT_GUARDS(a);
        const int n = kudroid_sscanf_trampoline("1 2", "%d %Q", &a.v);
        Check(n == 1, "an unknown conversion stops the scan");
        Check(a.v == 1 && GUARDS_OK(a), "what was already stored is intact");
    }
    /* A null destination must be tolerated: guest code does pass one, and a scanner that
     * dereferences it crashes inside a parse. */
    {
        const int n = kudroid_sscanf_trampoline("5", "%d", (void*)0);
        Check(n == 1, "a null destination is counted but not written");
    }
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
    RunScanChecks();

    puts_("\n");
    if (failures == 0) {
        puts_("=== vararg trampoline PASSED ===\n");
        sys_exit(0);
    }
    puts_("=== vararg trampoline FAILED ===\n");
    sys_exit(failures);
    return failures;
}
