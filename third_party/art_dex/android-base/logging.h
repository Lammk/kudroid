// Shim thay android-base/logging.h — chỉ phần libdexfile dùng: LOG/PLOG,
// CHECK*/DCHECK*, LogSeverity. Không có logd/palette; output đi ra stderr qua
// một hook để KuDroid chuyển vào pipeline log của mình.
#ifndef KUDROID_ANDROID_BASE_LOGGING_H_
#define KUDROID_ANDROID_BASE_LOGGING_H_

#include <errno.h>
#include <string.h>

#include <cstdlib>
#include <ostream>
#include <sstream>
#include <string>

#include "android-base/macros.h"

namespace android {
namespace base {

enum LogSeverity {
    VERBOSE,
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL_WITHOUT_ABORT,
    FATAL,
};

// KuDroid gắn hàm này để log chảy vào kudroid_android_log_message thay vì stderr.
using LogLineHook = void (*)(LogSeverity severity, const char* msg);
void SetLogLineHook(LogLineHook hook);

// Ngưỡng lọc: message thấp hơn bị bỏ. Mặc định INFO.
void SetMinimumLogSeverity(LogSeverity severity);
LogSeverity GetMinimumLogSeverity();

class LogMessage {
public:
    LogMessage(const char* file, unsigned int line, LogSeverity severity, int error);
    ~LogMessage();

    std::ostream& stream() { return stream_; }

private:
    const char* const file_;
    const unsigned int line_;
    const LogSeverity severity_;
    const int error_;  // errno tại thời điểm tạo, -1 nếu không phải PLOG
    std::ostringstream stream_;

    DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

// Đổi ngưỡng log trong một scope rồi tự trả lại.
class ScopedLogSeverity {
public:
    explicit ScopedLogSeverity(LogSeverity new_severity)
        : old_(GetMinimumLogSeverity()) {
        SetMinimumLogSeverity(new_severity);
    }
    ~ScopedLogSeverity() { SetMinimumLogSeverity(old_); }

private:
    LogSeverity old_;
    DISALLOW_COPY_AND_ASSIGN(ScopedLogSeverity);
};

// Hai hàm dưới chỉ để code AOSP tham chiếu tới compile được; KuDroid dùng hook.
void InitLogging(char** argv, LogLineHook hook = nullptr);
void StderrLogger(LogSeverity severity, const char* msg);
void LogdLogger(LogSeverity severity, const char* msg);

bool ShouldLog(LogSeverity severity);

}  // namespace base
}  // namespace android

#define LOG_STREAM(severity)                                            \
    ::android::base::LogMessage(__FILE__, __LINE__,                     \
                                ::android::base::severity, -1).stream()

#define LOG(severity)                                        \
    if (::android::base::ShouldLog(::android::base::severity)) \
        LOG_STREAM(severity)

#define PLOG(severity)                                                  \
    if (::android::base::ShouldLog(::android::base::severity))          \
        ::android::base::LogMessage(__FILE__, __LINE__,                 \
                                    ::android::base::severity, errno).stream()

#define LOG_ALWAYS_FATAL(...) LOG(FATAL)
#define LOG_FATAL_IF(cond, ...) \
    if (UNLIKELY(cond)) LOG(FATAL)

// CHECK luôn bật kể cả NDEBUG. Dùng do-while để `else` sau CHECK không dính.
#define CHECK(x)                                     \
    if (UNLIKELY(!(x)))                              \
        LOG_STREAM(FATAL) << "Check failed: " #x " "

#define KUDROID_CHECK_OP(LHS, RHS, OP)                                  \
    for (auto _values = ::android::base::MakeEagerEvaluator(LHS, RHS);   \
         UNLIKELY(!(_values.lhs OP _values.rhs));                       \
         /* empty */)                                                   \
        LOG_STREAM(FATAL) << "Check failed: " << #LHS " " #OP " " #RHS   \
                          << " (" #LHS "=" << _values.lhs               \
                          << ", " #RHS "=" << _values.rhs << ") "

#define CHECK_EQ(x, y) KUDROID_CHECK_OP(x, y, ==)
#define CHECK_NE(x, y) KUDROID_CHECK_OP(x, y, !=)
#define CHECK_LE(x, y) KUDROID_CHECK_OP(x, y, <=)
#define CHECK_LT(x, y) KUDROID_CHECK_OP(x, y, <)
#define CHECK_GE(x, y) KUDROID_CHECK_OP(x, y, >=)
#define CHECK_GT(x, y) KUDROID_CHECK_OP(x, y, >)

#define CHECK_STREQ(s1, s2)                                            \
    if (UNLIKELY(strcmp(s1, s2) != 0))                                 \
        LOG_STREAM(FATAL) << "Check failed: " #s1 " == " #s2 " "
#define CHECK_STRNE(s1, s2)                                            \
    if (UNLIKELY(strcmp(s1, s2) == 0))                                 \
        LOG_STREAM(FATAL) << "Check failed: " #s1 " != " #s2 " "

namespace android {
namespace base {
// Giữ cả hai toán hạng bằng giá trị để chỉ đánh giá biểu thức MỘT lần —
// CHECK_EQ(f(), g()) không được gọi f/g hai lần. Phải constexpr vì bit_utils.h
// dùng DCHECK_* bên trong hàm constexpr (CLZ/CTZ).
template <typename LHS, typename RHS>
struct EagerEvaluator {
    constexpr EagerEvaluator(LHS l, RHS r) : lhs(l), rhs(r) {}
    LHS lhs;
    RHS rhs;
};

template <typename LHS, typename RHS>
constexpr EagerEvaluator<LHS, RHS> MakeEagerEvaluator(LHS lhs, RHS rhs) {
    return EagerEvaluator<LHS, RHS>(lhs, rhs);
}
}  // namespace base
}  // namespace android

#if defined(NDEBUG) && !defined(ART_ENABLE_DCHECK)
static constexpr bool kEnableDChecks = false;
#else
static constexpr bool kEnableDChecks = true;
#endif

#define DCHECK(x)          if (kEnableDChecks) CHECK(x)
#define DCHECK_EQ(x, y)    if (kEnableDChecks) CHECK_EQ(x, y)
#define DCHECK_NE(x, y)    if (kEnableDChecks) CHECK_NE(x, y)
#define DCHECK_LE(x, y)    if (kEnableDChecks) CHECK_LE(x, y)
#define DCHECK_LT(x, y)    if (kEnableDChecks) CHECK_LT(x, y)
#define DCHECK_GE(x, y)    if (kEnableDChecks) CHECK_GE(x, y)
#define DCHECK_GT(x, y)    if (kEnableDChecks) CHECK_GT(x, y)
#define DCHECK_STREQ(x, y) if (kEnableDChecks) CHECK_STREQ(x, y)
#define DCHECK_STRNE(x, y) if (kEnableDChecks) CHECK_STRNE(x, y)

#define DCHECK_CONSTEXPR(x, out, dummy) DCHECK(x)

#define UNIMPLEMENTED(level) \
    LOG(level) << __PRETTY_FUNCTION__ << " unimplemented "

#endif  // KUDROID_ANDROID_BASE_LOGGING_H_
