#include "kudroid/abi/Sendfile.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/sendfile.h>
#endif

namespace kudroid {

ssize_t sendfileEmulated(int outFd, int inFd, off_t* offset, size_t count) {
    const int flags = ::fcntl(outFd, F_GETFL);
    if (flags == -1) return -1;
    if ((flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
    if (flags & O_APPEND) { errno = EINVAL; return -1; }
    const int inputFlags = ::fcntl(inFd, F_GETFL);
    if (inputFlags == -1) return -1;
    if ((inputFlags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
    off_t position = offset ? *offset : ::lseek(inFd, 0, SEEK_CUR);
    if (position < 0) {
        if (offset) errno = EINVAL;
        return -1;
    }
    count = std::min(count, size_t(0x7ffff000));
    char buffer[65536];
    size_t total = 0;
    int failure = 0;
    while (total < count) {
        const ssize_t read = ::pread(inFd, buffer, std::min(sizeof(buffer), count - total), position);
        if (read == 0) break;
        if (read < 0) { failure = errno; break; }
        const ssize_t written = ::write(outFd, buffer, size_t(read));
        if (written < 0) { failure = errno; break; }
        if (written == 0) { failure = EIO; break; }
        position += written;
        total += size_t(written);
        if (written != read) break;
    }
    if (offset) *offset = position;
    else if (total && ::lseek(inFd, position, SEEK_SET) < 0) failure = errno;
    if (failure) errno = failure;
    return total ? ssize_t(total) : (failure ? -1 : 0);
}

} // namespace kudroid

extern "C" ssize_t bionic_sendfile(int outFd, int inFd, off_t* offset, size_t count) {
#if defined(__linux__)
    return ::sendfile(outFd, inFd, offset, count);
#else
    return kudroid::sendfileEmulated(outFd, inFd, offset, count);
#endif
}
