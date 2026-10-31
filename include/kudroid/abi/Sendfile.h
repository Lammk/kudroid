#pragma once

#include <cstddef>
#include <sys/types.h>

namespace kudroid {
ssize_t sendfileEmulated(int outFd, int inFd, off_t* offset, size_t count);
}

extern "C" ssize_t bionic_sendfile(int outFd, int inFd, off_t* offset, size_t count);
