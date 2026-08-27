// Shim thay libbase của AOSP — chỉ những macro mà libdexfile thực sự dùng.
#ifndef KUDROID_ANDROID_BASE_MACROS_H_
#define KUDROID_ANDROID_BASE_MACROS_H_

#include <stddef.h>
#include <errno.h>
// AOSP lấy stdint qua chuỗi include của Android build; ngoài cây AOSP nhiều header
// ART dùng uint32_t mà không tự include — kéo vào đây một lần cho cả cây.
#include <stdint.h>

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__((__unused__))
#endif

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete
#endif

#ifndef DISALLOW_IMPLICIT_CONSTRUCTORS
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
  TypeName() = delete;                           \
  DISALLOW_COPY_AND_ASSIGN(TypeName)
#endif

// arraysize an toàn hơn sizeof/sizeof: chỉ khớp với mảng thật, không khớp con trỏ.
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#ifndef LIKELY
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#ifndef WARN_UNUSED
#define WARN_UNUSED __attribute__((warn_unused_result))
#endif

#ifndef UNUSED
#define UNUSED(...) ::kudroid_base::UnusedHelper(__VA_ARGS__)
namespace kudroid_base {
template <typename... T>
inline void UnusedHelper(const T&...) {}
}  // namespace kudroid_base
#endif

#ifndef FALLTHROUGH_INTENDED
#if defined(__clang__)
#define FALLTHROUGH_INTENDED [[clang::fallthrough]]
#else
#define FALLTHROUGH_INTENDED __attribute__((fallthrough))
#endif
#endif

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp)            \
  ({                                       \
    decltype(exp) _rc;                      \
    do {                                    \
      _rc = (exp);                          \
    } while (_rc == -1 && errno == EINTR);  \
    _rc;                                    \
  })
#endif

#endif  // KUDROID_ANDROID_BASE_MACROS_H_
