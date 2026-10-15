/* Freestanding AAPCS64 verification of kudroid_jni_call.
 * The host toolchain has an aarch64 cross-compiler but no aarch64 libc sysroot,
 * so this links with -nostdlib and talks to the kernel directly. Exit status is
 * the number of failed checks; each failure also prints its index. */
typedef unsigned long long u64;
typedef unsigned int u32;

extern u64 kudroid_jni_call(const void* fn, const u64* gp, unsigned ngp,
                            const u64* fp, unsigned nfp, u64* fp_ret);

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
static void put_num(int v) {
    char b[12]; int i = 11; b[i--] = '\n';
    if (v == 0) b[i--] = '0';
    while (v > 0) { b[i--] = (char)('0' + v % 10); v /= 10; }
    sys_write(1, b + i + 1, (unsigned long)(11 - i));
}

static int failures = 0;
static void Check(int ok, const char* what) {
    puts_(ok ? "  OK   " : "  FAIL ");
    puts_(what);
    puts_("\n");
    if (!ok) ++failures;
}

/* Natives declared exactly as a guest .so would declare them, so the compiler
 * picks the genuine AAPCS64 registers rather than anything KuART chose. */
float  f_id(void* e, void* c, float v)           { (void)e;(void)c; return v; }
double d_id(void* e, void* c, double v)          { (void)e;(void)c; return v; }
float  f_add(void* e, void* c, float a, float b) { (void)e;(void)c; return a + b; }
int    f_to_i(void* e, void* c, float v)         { (void)e;(void)c; return (int)(v * 2.0f); }
float  i_to_f(void* e, void* c, int v)           { (void)e;(void)c; return (float)v / 4.0f; }
double mixed(void* e, void* c, int i, double d, int j, float f) {
    (void)e;(void)c; return (double)i + d + (double)j + (double)f;
}
long long l_d(void* e, void* c, long long l, double d) { (void)e;(void)c; return l + (long long)d; }
/* 6 ints after env+cls == x0..x7, the AAPCS64 maximum. */
int six_ints(void* e, void* c, int a, int b, int cc, int d, int ee, int f) {
    (void)e;(void)c; return a + b + cc + d + ee + f;
}
/* 8 floats fill v0-v7 while the GP file stays at 2. */
float eight_f(void* e, void* c, float a, float b, float cc, float d,
              float ee, float f, float g, float h) {
    (void)e;(void)c; return a + b + cc + d + ee + f + g + h;
}
void* ptr_id(void* e, void* c, void* p) { (void)e;(void)c; return p; }

static u64 FBits(float f)  { u32 b; __builtin_memcpy(&b, &f, 4); return (u64)b; }
static u64 DBits(double d) { u64 b; __builtin_memcpy(&b, &d, 8); return b; }
static float AsF(u64 r)    { u32 b = (u32)r; float f; __builtin_memcpy(&f, &b, 4); return f; }
static double AsD(u64 r)   { double d; __builtin_memcpy(&d, &r, 8); return d; }
static int NearF(float a, float b) { float d = a - b; return d < 1e-6f && d > -1e-6f; }
static int NearD(double a, double b) { double d = a - b; return d < 1e-12 && d > -1e-12; }

static u64 gp[8], fp[8], fpret;
static char fake_env[16];

static void reset(void) {
    for (int i = 0; i < 8; ++i) { gp[i] = 0; fp[i] = 0; }
    fpret = 0;
    gp[0] = (u64)(void*)fake_env;   /* JNIEnv* */
    gp[1] = 0xC1A55;                /* jclass  */
}

void _start(void) {
    puts_("=== AAPCS64 trampoline (arm64 under qemu) ===\n");

    reset(); fp[0] = FBits(1.5f);
    kudroid_jni_call((void*)&f_id, gp, 2, fp, 1, &fpret);
    Check(NearF(AsF(fpret), 1.5f), "f_id(1.5f) -> 1.5f (float arg v0, float return v0)");

    reset(); fp[0] = FBits(-0.25f);
    kudroid_jni_call((void*)&f_id, gp, 2, fp, 1, &fpret);
    Check(NearF(AsF(fpret), -0.25f), "f_id(-0.25f) -> -0.25f (sign preserved)");

    reset(); fp[0] = DBits(2.75);
    kudroid_jni_call((void*)&d_id, gp, 2, fp, 1, &fpret);
    Check(NearD(AsD(fpret), 2.75), "d_id(2.75) -> 2.75");

    reset(); fp[0] = FBits(1.25f); fp[1] = FBits(2.5f);
    kudroid_jni_call((void*)&f_add, gp, 2, fp, 2, &fpret);
    Check(NearF(AsF(fpret), 3.75f), "f_add(1.25f, 2.5f) -> 3.75f (v0, v1)");

    reset(); fp[0] = FBits(21.0f);
    Check((int)kudroid_jni_call((void*)&f_to_i, gp, 2, fp, 1, &fpret) == 42,
          "f_to_i(21.0f) -> 42 (float arg, int return in x0)");

    reset(); gp[2] = 10;
    kudroid_jni_call((void*)&i_to_f, gp, 3, fp, 0, &fpret);
    Check(NearF(AsF(fpret), 2.5f), "i_to_f(10) -> 2.5f (int arg, float return)");

    /* i->x2, d->v0, j->x3, f->v1: the two files have independent budgets. */
    reset(); gp[2] = 1; gp[3] = 2; fp[0] = DBits(0.5); fp[1] = FBits(0.25f);
    kudroid_jni_call((void*)&mixed, gp, 4, fp, 2, &fpret);
    Check(NearD(AsD(fpret), 3.75),
          "mixed(1, 0.5, 2, 0.25f) -> 3.75 (GP and FP budgets independent)");

    reset(); gp[2] = 1000; fp[0] = DBits(24.0);
    Check((long long)kudroid_jni_call((void*)&l_d, gp, 3, fp, 1, &fpret) == 1024,
          "l_d(1000L, 24.0) -> 1024 (64-bit int vs 64-bit float)");

    reset(); for (int i = 0; i < 6; ++i) gp[2 + i] = (u64)(i + 1);
    Check((int)kudroid_jni_call((void*)&six_ints, gp, 8, fp, 0, &fpret) == 21,
          "six_ints(1..6) -> 21 (all 8 GP registers x0-x7)");

    reset(); for (int i = 0; i < 8; ++i) fp[i] = FBits((float)(i + 1));
    kudroid_jni_call((void*)&eight_f, gp, 2, fp, 8, &fpret);
    Check(NearF(AsF(fpret), 36.0f),
          "eight_f(1..8) -> 36.0f (all 8 FP registers v0-v7, GP nearly empty)");

    reset(); gp[2] = (u64)(void*)fake_env;
    Check(kudroid_jni_call((void*)&ptr_id, gp, 3, fp, 0, &fpret) == (u64)(void*)fake_env,
          "ptr_id round-trips a pointer unchanged");

    reset(); for (int i = 0; i < 6; ++i) gp[2 + i] = 1;
    Check((int)kudroid_jni_call((void*)&six_ints, gp, 8, fp, 0, (u64*)0) == 6,
          "fp_ret == NULL is safe when no FP return is wanted");

    if (failures == 0) { puts_("=== arm64 trampoline PASSED ===\n"); sys_exit(0); }
    puts_("=== arm64 trampoline FAILED, count: ");
    put_num(failures);
    sys_exit(failures);
}
