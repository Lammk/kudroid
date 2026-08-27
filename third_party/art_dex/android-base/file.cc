#include "android-base/file.h"
#include "android-base/macros.h"

#include <fcntl.h>
#include <unistd.h>

namespace android {
namespace base {

bool ReadFdToString(int fd, std::string* content) {
    content->clear();
    char buf[4096];
    ssize_t n;
    while ((n = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(buf)))) > 0) {
        content->append(buf, n);
    }
    return n == 0;
}

bool ReadFileToString(const std::string& path, std::string* content, bool follow_symlinks) {
    content->clear();
    int flags = O_RDONLY | O_CLOEXEC;
    if (!follow_symlinks) flags |= O_NOFOLLOW;
    int fd = TEMP_FAILURE_RETRY(open(path.c_str(), flags));
    if (fd == -1) return false;
    bool result = ReadFdToString(fd, content);
    close(fd);
    return result;
}

bool WriteFully(int fd, const void* data, size_t byte_count) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t remaining = byte_count;
    while (remaining > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, p, remaining));
        if (n == -1) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

bool ReadFully(int fd, void* data, size_t byte_count) {
    uint8_t* p = static_cast<uint8_t*>(data);
    size_t remaining = byte_count;
    while (remaining > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(read(fd, p, remaining));
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    return true;
}

bool WriteStringToFile(const std::string& content, const std::string& path,
                       bool follow_symlinks) {
    int flags = O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC;
    if (!follow_symlinks) flags |= O_NOFOLLOW;
    int fd = TEMP_FAILURE_RETRY(open(path.c_str(), flags, 0666));
    if (fd == -1) return false;
    bool result = WriteFully(fd, content.data(), content.size());
    close(fd);
    return result;
}

}  // namespace base
}  // namespace android
