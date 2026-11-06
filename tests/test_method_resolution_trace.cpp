#include "kudroid/kuart/MethodResolutionTrace.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace {
volatile sig_atomic_t reportFd = -1;
void signalReport(int) { kudroid::kuart::WriteMethodResolutionTrace(reportFd, 17); }
std::string readReport(int fd) {
    lseek(fd, 0, SEEK_SET);
    std::string result;
    char chunk[1024];
    ssize_t n;
    while ((n = read(fd, chunk, sizeof(chunk))) > 0) result.append(chunk, n);
    return result;
}
}

int main() {
    char path[] = "/tmp/kudroid-resolution-trace-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 2;
    unlink(path);
    int failures = 0, checks = 0;
    auto check = [&](bool ok, const char* label) {
        ++checks;
        failures += !ok;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    };
    kudroid::kuart::RecordMethodResolution(17, "old-entry");
    for (int i = 0; i < 80; ++i) kudroid::kuart::RecordMethodResolution(29, "other-thread");
    std::string copied = "source=fast-cache target=0x1234";
    kudroid::kuart::RecordMethodResolution(17, copied.c_str());
    copied.assign("changed-after-recording");
    kudroid::kuart::RecordMethodResolution(17, std::string(1024, 'x').c_str());
    reportFd = fd;
    struct sigaction action{};
    action.sa_handler = signalReport;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, nullptr) != 0) return 2;
    errno = EDOM;
    raise(SIGUSR1);
    check(errno == EDOM, "signal reporting preserves errno");
    const auto report = readReport(fd);
    check(report.find("source=fast-cache target=0x1234") != std::string::npos &&
          report.find("changed-after-recording") == std::string::npos,
          "crash reporting uses copied metadata");
    check(report.find("other-thread") == std::string::npos, "history filters the faulting thread");
    check(report.find("old-entry") == std::string::npos, "history evicts old entries");
    check(report.size() < 800 && report.back() == '\n', "long records remain bounded and terminated");
    close(fd);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
