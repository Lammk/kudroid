// Global VM lock and Java object monitors.
//
// Only one thread may interpret bytecode at a time: DexHeap, DexClassLinker's
// maps, DexJniEnv's ref tables and Interpreter's own depth_/pending_exception_
// are all unsynchronised. Java threads therefore run interleaved rather than in
// parallel. Blocking operations (Thread.sleep, Object.wait, contended
// monitor-enter) drop the lock so another thread can make progress.
#ifndef KUDROID_KUART_VMLOCK_H
#define KUDROID_KUART_VMLOCK_H

#include <cstdint>

namespace kudroid {
namespace kuart {

class DexObject;

// Held for the whole duration of a bytecode call made from outside the
// interpreter (Interpreter::Execute at depth 0, and each Java thread body).
class VmLockGuard {
public:
    VmLockGuard();
    ~VmLockGuard();
    VmLockGuard(const VmLockGuard&) = delete;
    VmLockGuard& operator=(const VmLockGuard&) = delete;
};

// Drops the VM lock for the duration of a blocking call, then takes it back.
// No-op on a thread that does not hold it.
class VmLockRelease {
public:
    VmLockRelease();
    ~VmLockRelease();
    VmLockRelease(const VmLockRelease&) = delete;
    VmLockRelease& operator=(const VmLockRelease&) = delete;

private:
    int depth_;
};

// How many times this thread currently holds the VM lock.
int VmLockDepth();

// Release the VM lock down to `depth`, for a caller that left the interpreter
// without unwinding the C++ stack.
//
// VmLockGuard is RAII, which siglongjmp skips: the JNI_OnLoad shield jumps out of a
// faulting library from a signal handler and the guard's destructor never runs, so
// the lock is never released. The jumping thread does not notice — the mutex is
// recursive, so it keeps re-entering its own lock — but every OTHER Java thread
// blocks on it permanently. Nothing in the log would point at the load that caused
// it, since the library it happened in was skipped and reported as merely a warning.
void VmLockUnwindTo(int depth);

// Per-object monitor for `synchronized` and wait/notify. State lives in
// DexObject::lock_owner_tid / lock_count, guarded by an internal mutex.
//
// Every function returns false and leaves an error message in `error` when the
// caller does not own the monitor; the caller turns that into
// IllegalMonitorStateException.
namespace Monitor {

// Unique small id for the calling thread; 0 is reserved for "unowned".
uint32_t SelfThreadId();

// Recursive: re-entering a monitor this thread already owns only bumps the
// count. Blocks (releasing the VM lock) while another thread owns it.
void Enter(DexObject* obj);

bool Exit(DexObject* obj);

// Releases the monitor entirely, waits to be notified, then re-acquires it with
// the same recursion count. `millis == 0 && nanos == 0` waits indefinitely.
bool Wait(DexObject* obj, int64_t millis, int32_t nanos);

bool Notify(DexObject* obj, bool all);

}  // namespace Monitor

}  // namespace kuart
}  // namespace kudroid

#endif  // KUDROID_KUART_VMLOCK_H
