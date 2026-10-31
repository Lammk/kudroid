The framework implementations of AbstractOwnableSynchronizer,
AbstractQueuedSynchronizer, ReentrantLock, ThreadPoolExecutor and
PriorityBlockingQueue derive from
Android libcore tag android-10.0.0_r47, ojluni/src/main/java/java/util/concurrent/.

Source: https://android.googlesource.com/platform/libcore/+/refs/tags/android-10.0.0_r47/ojluni/src/main/java/java/util/concurrent/

The original copyright and license notices remain in each source file.
openjdk-concurrency-LICENSE includes GPL version 2 and the Classpath exception.

Modifications on 2026-10-30: condensed source comments, omitted runtime-specific
annotations and the unavailable SecurityManager branch, retained the framework's
ExecutorService hierarchy and submit implementations, and adapted purge to
remove canceled futures through queue removal rather than iterator removal.
The synchronizer algorithms and lock semantics are retained.

PriorityBlockingQueue retains the Android 10 heap, lock, condition, growth,
interruption, serialization and snapshot-iterator algorithms. Its string
formatting uses a local array snapshot in place of the libcore Helpers utility.
