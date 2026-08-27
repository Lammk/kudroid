#ifndef KUDROID_ANDROID_BASE_THREAD_ANNOTATIONS_H_
#define KUDROID_ANDROID_BASE_THREAD_ANNOTATIONS_H_

// Clang thread-safety analysis attributes. Không có clang thì rỗng.
#if defined(__clang__) && defined(__has_attribute)
#define KUDROID_TSA(x) __attribute__((x))
#else
#define KUDROID_TSA(x)
#endif

#define CAPABILITY(x)                KUDROID_TSA(capability(x))
#define SHARED_CAPABILITY(x)         KUDROID_TSA(shared_capability(x))
#define SCOPED_CAPABILITY            KUDROID_TSA(scoped_lockable)
#define GUARDED_BY(x)                KUDROID_TSA(guarded_by(x))
#define PT_GUARDED_BY(x)             KUDROID_TSA(pt_guarded_by(x))
#define ACQUIRED_BEFORE(...)         KUDROID_TSA(acquired_before(__VA_ARGS__))
#define ACQUIRED_AFTER(...)          KUDROID_TSA(acquired_after(__VA_ARGS__))
#define REQUIRES(...)                KUDROID_TSA(requires_capability(__VA_ARGS__))
#define REQUIRES_SHARED(...)         KUDROID_TSA(requires_shared_capability(__VA_ARGS__))
#define ACQUIRE(...)                 KUDROID_TSA(acquire_capability(__VA_ARGS__))
#define ACQUIRE_SHARED(...)          KUDROID_TSA(acquire_shared_capability(__VA_ARGS__))
#define RELEASE(...)                 KUDROID_TSA(release_capability(__VA_ARGS__))
#define RELEASE_SHARED(...)          KUDROID_TSA(release_shared_capability(__VA_ARGS__))
#define TRY_ACQUIRE(...)             KUDROID_TSA(try_acquire_capability(__VA_ARGS__))
#define TRY_ACQUIRE_SHARED(...)      KUDROID_TSA(try_acquire_shared_capability(__VA_ARGS__))
#define EXCLUDES(...)                KUDROID_TSA(locks_excluded(__VA_ARGS__))
#define ASSERT_CAPABILITY(x)         KUDROID_TSA(assert_capability(x))
#define ASSERT_SHARED_CAPABILITY(x)  KUDROID_TSA(assert_shared_capability(x))
#define RETURN_CAPABILITY(x)         KUDROID_TSA(lock_returned(x))
#define NO_THREAD_SAFETY_ANALYSIS    KUDROID_TSA(no_thread_safety_analysis)
#define EXCLUSIVE_LOCKS_REQUIRED(...) REQUIRES(__VA_ARGS__)
#define SHARED_LOCKS_REQUIRED(...)    REQUIRES_SHARED(__VA_ARGS__)
#define LOCKS_EXCLUDED(...)           EXCLUDES(__VA_ARGS__)

#endif  // KUDROID_ANDROID_BASE_THREAD_ANNOTATIONS_H_
