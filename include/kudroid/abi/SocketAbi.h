// Linux (guest) to Darwin (host) translation for sockets and fcntl.
//
// A guest is compiled against Linux headers, so the numbers it passes and expects back —
// socket option names, MSG_* flags, fcntl commands and O_* flags, errno values — are
// Linux's. The host libc is Darwin's, where nearly all of them differ: SOL_SOCKET is 1
// there and 0xffff here, O_NONBLOCK is 0x800 there and 0x4 here, EINPROGRESS is 115 there
// and 36 here. Forwarded unchanged, setsockopt/getsockopt/fcntl fail or do something else,
// which a client that checks a connection with getsockopt(SO_ERROR) reads as "not
// connected" after every successful connect().
//
// Everything below is pure arithmetic over literal numbers for both sides, never the
// host's macros, so the tables run and are tested on any host.
#ifndef KUDROID_ABI_SOCKETABI_H
#define KUDROID_ABI_SOCKETABI_H

#include <stddef.h>
#include <stdint.h>

struct sockaddr;

namespace kudroid {

// Darwin errno -> the Linux number a guest compares against. Values with no Linux
// counterpart in the socket range, and everything outside it, pass through unchanged.
int SocketErrnoToGuest(int darwinErrno);

// Linux (level, optname) -> Darwin. False when Darwin has no equivalent.
bool SocketOptionToHost(int level, int name, int* hostLevel, int* hostName);

// Linux MSG_* flags -> Darwin. Flags Darwin lacks are dropped.
int SocketMsgFlagsToHost(int linuxFlags);

// File status flags (F_GETFL/F_SETFL): the access mode plus O_NONBLOCK, O_APPEND, O_ASYNC.
int StatusFlagsToHost(int linuxFlags);
int StatusFlagsToGuest(int darwinFlags);

// fcntl command -> Darwin, or -1 when Darwin has no equivalent.
int FcntlCommandToHost(int linuxCommand);

// struct flock, laid out as each side's headers define it.
struct LinuxFlock {
    int16_t l_type;
    int16_t l_whence;
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
};

struct DarwinFlock {
    int64_t l_start;
    int64_t l_len;
    int32_t l_pid;
    int16_t l_type;
    int16_t l_whence;
};

void FlockToHost(const LinuxFlock& guest, DarwinFlock* host);
void FlockToGuest(const DarwinFlock& host, LinuxFlock* guest);

// struct addrinfo as laid out by Bionic LP64:
// ai_canonname is at offset 24, ai_addr is at offset 32.
struct GuestAddrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    uint32_t ai_addrlen;
    char* ai_canonname;
    struct sockaddr* ai_addr;
    struct GuestAddrinfo* ai_next;
};

// Bionic AI_* flags to Darwin or glibc; -1 for unsupported flags.
int AddrInfoFlagsToHost(int guestFlags);
int AddrInfoFlagsToLinux(int guestFlags);
int NameInfoFlagsToLinux(int guestFlags);
int AddrInfoErrorToGuest(int linuxError);
int AddrInfoErrorToLinux(int guestError);

// struct hostent, as both sides' LP64 headers lay it out.
struct GuestHostent {
    char* h_name;
    char** h_aliases;
    int h_addrtype;
    int h_length;
    char** h_addr_list;
};

// Copies `src` into `dst` with every string and array placed inside buf (the reentrant
// gethostbyname_r contract), reporting `guestAddrType` as h_addrtype. Returns 0, or 34
// (ERANGE) when buf is too small to hold the copy.
int HostentCopy(const GuestHostent& src, int guestAddrType, GuestHostent* dst, char* buf,
                size_t buflen);

}  // namespace kudroid

#endif
