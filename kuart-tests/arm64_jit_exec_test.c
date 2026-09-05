// Execute JitCompiler output on real arm64 under qemu: golden bytes prove the encoding,
// this proves the sequences COMPUTE the right values. Freestanding (svc only, no sysroot).
typedef unsigned long long u64; typedef unsigned int u32;
typedef long long i64; typedef int i32;
#include "gen_code.h"

static u64 slen(const char*s){u64 n=0;while(s[n])n++;return n;}
static void out(const char* s){
  register long x0 asm("x0")=1; register const char* x1 asm("x1")=s;
  register long x2 asm("x2")=(long)slen(s); register long x8 asm("x8")=64;
  asm volatile("svc 0"::"r"(x0),"r"(x1),"r"(x2),"r"(x8):"memory"); }
static void die(int c){ register long x0 asm("x0")=c; register long x8 asm("x8")=93;
  asm volatile("svc 0"::"r"(x0),"r"(x8)); __builtin_unreachable(); }
static void pnum(i64 v){ char b[24]; int n=0; if(v<0){out("-");v=-v;}
  if(!v){out("0");return;} while(v){b[n++]='0'+(v%10);v/=10;}
  char r[25]; int i=0; while(n)r[i++]=b[--n]; r[i]=0; out(r); }

static int fails=0;
static void ck(const char* n, i64 got, i64 want){
  if(got==want){out("  OK   ");out(n);out("\n");}
  else{out("  FAIL ");out(n);out(" got ");pnum(got);out(" want ");pnum(want);out("\n");fails++;}
}

typedef union { i32 i; i64 j; u64 raw; } DexValue;
typedef i32 (*Entry)(DexValue* regs, DexValue* res, u32* pc);

// mmap RWX and copy the code in.
//
// JitCache maps RW then mprotects to RX, which is the right shape for a process that
// must not hold writable-executable pages. Here the point is to run the bytes, and
// qemu-user grants RWX directly, so the extra step would test the harness rather than
// the compiler.
static void* jitbuf(u32* code, int n){
  register long x0 asm("x0")=0; register long x1 asm("x1")=4096;
  register long x2 asm("x2")=7;      // PROT_READ|WRITE|EXEC
  register long x3 asm("x3")=0x22;   // MAP_PRIVATE|MAP_ANONYMOUS
  register long x4 asm("x4")=-1; register long x5 asm("x5")=0;
  register long x8 asm("x8")=222;    // mmap
  register long ret asm("x0");
  asm volatile("svc 0":"=r"(ret):"r"(x0),"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x8):"memory");
  u32* p=(u32*)ret; for(int i=0;i<n;i++)p[i]=code[i];
  // Flush the caches by hand: freestanding has no __clear_cache, and arm64 needs the
  // data cache written back and the instruction cache invalidated before newly written
  // code can be executed. Skipping this runs whatever previously occupied the address.
  for(int i=0;i<n;i++){ asm volatile("dc cvau, %0"::"r"(&p[i]):"memory"); }
  asm volatile("dsb ish; isb":::"memory");
  for(int i=0;i<n;i++){ asm volatile("ic ivau, %0"::"r"(&p[i]):"memory"); }
  asm volatile("dsb ish; isb":::"memory");
  return p;
}

void _start(void){
  out("=== JIT-generated arm64 code, executed ===\n");
  DexValue regs[8]; DexValue res; u32 pc=0;

  // ADD_INT: v2 = v0 + v1, return v2
  Entry add=(Entry)jitbuf(code_add, code_add_n);
  for(int k=0;k<8;k++)regs[k].raw=0;
  regs[0].i=40; regs[1].i=2;
  ck("add_int 40+2 returns", add(regs,&res,&pc), 0);
  ck("add_int result", res.i, 42);
  ck("add_int wrote v2", regs[2].i, 42);
  // Java int arithmetic wraps at 32 bits; the slot is 64, so the clamp must apply.
  regs[0].i=2147483647; regs[1].i=1;
  add(regs,&res,&pc);
  ck("add_int overflow wraps", res.i, -2147483648);
  ck("add_int upper 32 bits are clean", (i64)(res.raw>>32), 0);
  // Negative operands: the 32-bit form must sign-extend on load.
  regs[0].i=-5; regs[1].i=3; add(regs,&res,&pc);
  ck("add_int -5+3", res.i, -2);

  // MUL_LONG
  Entry mull=(Entry)jitbuf(code_mull, code_mull_n);
  regs[0].j=6000000000LL; regs[1].j=3;
  ck("mul_long returns", mull(regs,&res,&pc), 0);
  ck("mul_long result", res.j, 18000000000LL);
  regs[0].j=-7; regs[1].j=6; mull(regs,&res,&pc);
  ck("mul_long negative", res.j, -42);

  // CMP_LONG yields -1/0/1. Inverting cset here would negate every result.
  Entry cmpl=(Entry)jitbuf(code_cmpl, code_cmpl_n);
  regs[0].j=5; regs[1].j=3; cmpl(regs,&res,&pc);
  ck("cmp_long 5 vs 3 = 1", res.i, 1);
  regs[0].j=3; regs[1].j=5; cmpl(regs,&res,&pc);
  ck("cmp_long 3 vs 5 = -1", res.i, -1);
  regs[0].j=4; regs[1].j=4; cmpl(regs,&res,&pc);
  ck("cmp_long 4 vs 4 = 0", res.i, 0);
  regs[0].j=-9223372036854775807LL-1; regs[1].j=9223372036854775807LL;
  cmpl(regs,&res,&pc);
  ck("cmp_long INT64_MIN vs MAX = -1", res.i, -1);

  // Loop: branch patching in both directions, and the loop-exit condition.
  Entry loop=(Entry)jitbuf(code_loop, code_loop_n);
  for(int k=0;k<8;k++)regs[k].raw=0;
  regs[1].i=10; loop(regs,&res,&pc);
  ck("loop sum 1..10", res.i, 55);
  regs[1].i=100; loop(regs,&res,&pc);
  ck("loop sum 1..100", res.i, 5050);
  regs[1].i=1; loop(regs,&res,&pc);
  ck("loop sum 1..1", res.i, 1);
  regs[1].i=0; loop(regs,&res,&pc);
  ck("loop with zero iterations", res.i, 0);
  regs[1].i=-3; loop(regs,&res,&pc);
  ck("loop with negative bound does not run", res.i, 0);

  if(fails){ out("=== FAILED ===\n"); die(1); }
  out("=== JIT execution PASSED ===\n"); die(0);
}
