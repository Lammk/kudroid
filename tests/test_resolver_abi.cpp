#include "kudroid/abi/SocketAbi.h"
#include <cstdio>
#include <cstring>
#include <sys/socket.h>

extern "C" int bionic_getaddrinfo(const char*, const char*, const kudroid::GuestAddrinfo*,
                                   kudroid::GuestAddrinfo**);
extern "C" void bionic_freeaddrinfo(kudroid::GuestAddrinfo*);
extern "C" int bionic_getnameinfo(const sockaddr*, socklen_t, char*, socklen_t, char*, socklen_t, int);
extern "C" const char* bionic_gai_strerror(int);

static int failures;
static void check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    failures += !ok;
}
int main() {
    kudroid::GuestAddrinfo hints{};
    hints.ai_family = 2;
    hints.ai_socktype = 1;
    hints.ai_flags = 0x4 | 0x8;
    kudroid::GuestAddrinfo* result = nullptr;
    int rc = bionic_getaddrinfo("127.0.0.1", "80", &hints, &result);
    check(rc == 0 && result && result->ai_addr && result->ai_family == 2,
          "Bionic numeric host/service resolves without DNS");
    if (result && result->ai_addr) {
        const auto* address = reinterpret_cast<const unsigned char*>(result->ai_addr);
        check(result->ai_addrlen == 16 && address[0] == 2 && address[2] == 0 && address[3] == 80
                  && address[4] == 127, "Bionic addrinfo exposes address pointer and network byte order");
        char host[128], service[32];
        rc = bionic_getnameinfo(result->ai_addr, result->ai_addrlen, host, sizeof(host), service,
                                sizeof(service), 0x2 | 0x8);
        check(rc == 0 && std::strcmp(host, "127.0.0.1") == 0 && std::strcmp(service, "80") == 0,
              "Bionic reverse lookup numeric flags retain numeric host and service");
        char tiny[1];
        check(bionic_getnameinfo(result->ai_addr, result->ai_addrlen, tiny, sizeof(tiny), nullptr,
                                  0, 0x2) == 14, "small name buffer returns Bionic EAI_OVERFLOW");
    }
    bionic_freeaddrinfo(result);
    result = nullptr;
    check(bionic_getaddrinfo("127.0.0.1", "http", &hints, &result) == 8 && !result,
          "numeric service rejects named port with Bionic EAI_NONAME");
    hints.ai_flags = 0x400;
    rc = bionic_getaddrinfo("localhost", "80", &hints, &result);
    check(rc == 0 && result, "Bionic configured-address flag supports local hostname lookup");
    bionic_freeaddrinfo(result);
    result = nullptr;
    hints.ai_flags = 0x2;
    rc = bionic_getaddrinfo("localhost", "80", &hints, &result);
    check(rc == 0 && result && result->ai_canonname && result->ai_addr,
          "canonical name and socket address use Bionic LP64 field order");
    bionic_freeaddrinfo(result);
    result = nullptr;
    hints.ai_family = 10;
    hints.ai_flags = 0x800 | 0x4 | 0x8;
    rc = bionic_getaddrinfo("127.0.0.1", "80", &hints, &result);
    check(rc == 0 && result && result->ai_family == 10 && result->ai_addrlen == 28,
          "Bionic mapped-address flag resolves IPv4 on IPv6 sockets");
    bionic_freeaddrinfo(result);
    hints.ai_flags = 0x1000;
    result = nullptr;
    check(bionic_getaddrinfo("127.0.0.1", "80", &hints, &result) == 3 && !result,
          "unsupported resolver flag returns Bionic EAI_BADFLAGS");
    check(bionic_getnameinfo(nullptr, 0, nullptr, 0, nullptr, 0, 0) == 5,
          "invalid socket address returns Bionic EAI_FAMILY");
    check(std::strstr(bionic_gai_strerror(8), "Unknown error") == nullptr,
          "Bionic lookup error has a host error description");
    return failures ? 1 : 0;
}
