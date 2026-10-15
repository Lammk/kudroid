// Thread sampler: read where OTHER threads are, from outside them.
//
// The problem this solves. Every diagnostic KuDroid had before this one is a thread
// reporting on itself — a shim logs on entry, the blocking-wait registry is written
// by the thread about to block, the telemetry watchdog reads a record the calling
// thread filled in. All of it shares one blind spot: a thread stuck somewhere that
// calls no shim says nothing at all.
//
// That blind spot is where ULTRAKILL stopped. The main thread entered
// UnityPlayer.nativeRender and never came out; the watchdog printed the same
// stage=before-trampoline line 40 times over 36 seconds, the blocking-wait registry
// held no entry for that thread, and the only stall reported belonged to an idle GC
// helper. Nothing in the process could say where the main thread actually was,
// because the main thread was not running any of our code.
//
// What this does instead. Walk the task's thread list and read each thread's
// register state directly: pc, lr, sp, run state, accumulated CPU time and name.
// That answers "where is this thread" for every cause at once — a spin, poll(),
// pthread_join, a GPU fence inside Metal, or a plain infinite loop in guest code —
// without the stuck thread cooperating.
//
// Why it does not suspend. Suspending a thread to get a consistent snapshot risks a
// far worse failure than a torn one: if the target holds the allocator's lock and
// this code then allocates, the process deadlocks for good, and it would do so
// inside the diagnostic meant to explain a hang. Reading a running thread's state
// can yield a pc that is already stale, which is acceptable — for a thread that is
// genuinely stuck, which is the case this exists for, the value is exact. Two
// samples taken seconds apart settle the rest: a pc that moved with CPU time
// climbing is a spin, a pc that did not move with CPU time flat is a park.
#ifndef KUDROID_DEBUG_THREADSAMPLER_H
#define KUDROID_DEBUG_THREADSAMPLER_H

namespace kudroid {

// Write one breadcrumb line per thread in this process, plus a begin/end pair
// carrying `reason` and a sequence number so two samples can be told apart and
// diffed. Returns the number of threads reported, or 0 if the platform gave nothing.
//
// Safe to call from the telemetry watchdog while guest threads are wedged: it takes
// no lock a guest thread could hold, suspends nothing, and formats into stack
// buffers. `reason` appears verbatim in the output and should say what prompted the
// sample.
int thread_sample_report(const char* reason);

}  // namespace kudroid

#endif  // KUDROID_DEBUG_THREADSAMPLER_H
