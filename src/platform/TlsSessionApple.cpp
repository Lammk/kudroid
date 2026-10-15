// TLS through SecureTransport. Its I/O callbacks never block: a short transfer returns
// errSSLWouldBlock and the waiting is done in poll() outside the session lock, so one thread
// can sit in a read while another writes. SecureTransport tops out at TLS 1.2.
#include "kudroid/platform/TlsSession.h"

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <Security/SecureTransport.h>

namespace kudroid {

class TlsSession {
public:
    int fd = -1;
    SSLContextRef ctx = nullptr;
    std::mutex mu;
    bool peerEof = false;
    bool wantRead = false;        // the last would-block was on the read side
    int writeTimeoutMs = 60000;   // how long a write may wait for socket space
    std::atomic<int> users{0};
    std::vector<std::vector<uint8_t>> chain;
    std::string lastError;
    std::string alpn;
};

namespace {

std::mutex g_liveMu;
std::set<TlsSession*> g_live;

struct Use {
    TlsSession* s;
    bool ok;
    explicit Use(TlsSession* session) : s(session), ok(false) {
        std::lock_guard<std::mutex> lock(g_liveMu);
        if (g_live.count(session) != 0) {
            ok = true;
            ++s->users;
        }
    }
    ~Use() {
        if (ok) --s->users;
    }
};

OSStatus ReadCallback(SSLConnectionRef connection, void* data, size_t* length) {
    auto* s = const_cast<TlsSession*>(static_cast<const TlsSession*>(connection));
    const size_t want = *length;
    size_t got = 0;
    while (got < want) {
        const ssize_t n = ::recv(s->fd, static_cast<uint8_t*>(data) + got, want - got, MSG_DONTWAIT);
        if (n > 0) {
            got += static_cast<size_t>(n);
            continue;
        }
        if (n == 0) {
            s->peerEof = true;
            *length = got;
            return errSSLClosedGraceful;
        }
        if (errno == EINTR) continue;
        *length = got;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            s->wantRead = true;
            return errSSLWouldBlock;
        }
        return errSSLClosedAbort;
    }
    *length = got;
    return noErr;
}

// Writes wait for socket space here instead of reporting would-block: a half-written record
// would otherwise have to be resumed by a retry protocol SecureTransport only loosely
// documents, and a full send buffer clears in moments.
OSStatus WriteCallback(SSLConnectionRef connection, const void* data, size_t* length) {
    auto* s = const_cast<TlsSession*>(static_cast<const TlsSession*>(connection));
    const size_t want = *length;
    size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(s->writeTimeoutMs);
    while (sent < want) {
        const ssize_t n = ::send(s->fd, static_cast<const uint8_t*>(data) + sent, want - sent, MSG_DONTWAIT);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) {
                *length = sent;
                return errSSLClosedAbort;
            }
            struct pollfd p = {s->fd, POLLOUT, 0};
            ::poll(&p, 1, static_cast<int>(std::min<long long>(left, 1000)));
            continue;
        }
        *length = sent;
        return errSSLClosedAbort;
    }
    *length = sent;
    return noErr;
}

using Clock = std::chrono::steady_clock;

int WaitFd(int fd, short events, Clock::time_point deadline, bool bounded) {
    for (;;) {
        int remaining = -1;
        if (bounded) {
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
                    .count();
            if (left <= 0) return 0;
            remaining = static_cast<int>(left);
        }
        struct pollfd p = {fd, events, 0};
        const int rc = ::poll(&p, 1, remaining);
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        if (errno != EINTR) return -errno;
    }
}

std::string StatusText(OSStatus st) {
    CFStringRef text = SecCopyErrorMessageString(st, nullptr);
    std::string out = "OSStatus " + std::to_string(static_cast<int>(st));
    if (text != nullptr) {
        char buf[256];
        if (CFStringGetCString(text, buf, sizeof(buf), kCFStringEncodingUTF8)) {
            out += ": ";
            out += buf;
        }
        CFRelease(text);
    }
    return out;
}

std::vector<std::vector<uint8_t>> ChainOf(SecTrustRef trust) {
    std::vector<std::vector<uint8_t>> out;
    CFArrayRef certs = SecTrustCopyCertificateChain(trust);
    if (certs == nullptr) return out;
    const CFIndex count = CFArrayGetCount(certs);
    for (CFIndex i = 0; i < count; ++i) {
        auto cert = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(certs, i)));
        CFDataRef data = SecCertificateCopyData(cert);
        if (data == nullptr) continue;
        const uint8_t* bytes = CFDataGetBytePtr(data);
        out.emplace_back(bytes, bytes + CFDataGetLength(data));
        CFRelease(data);
    }
    CFRelease(certs);
    return out;
}

void CaptureChain(TlsSession* s) {
    SecTrustRef trust = nullptr;
    if (SSLCopyPeerTrust(s->ctx, &trust) == noErr && trust != nullptr) {
        s->chain = ChainOf(trust);
        CFRelease(trust);
    }
}

// Runs a SecureTransport call until it stops asking to wait. Returns its OSStatus, or sets
// *failure to a negative code when the wait itself failed or timed out.
template <typename Op>
OSStatus Drive(TlsSession* s, int timeoutMs, Op op, int* failure) {
    const bool bounded = timeoutMs > 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(bounded ? timeoutMs : 0);
    *failure = 0;
    for (;;) {
        OSStatus st;
        bool waitRead;
        {
            std::lock_guard<std::mutex> lock(s->mu);
            s->wantRead = false;
            st = op();
            if (st != errSSLWouldBlock) return st;
            waitRead = true;  // only reads report would-block
        }
        const int ready = WaitFd(s->fd, waitRead ? POLLIN : POLLOUT, deadline, bounded);
        if (ready == 0) {
            *failure = -ETIMEDOUT;
            return errSSLWouldBlock;
        }
        if (ready < 0) {
            *failure = ready;
            return errSSLWouldBlock;
        }
    }
}

const char* CipherName(SSLCipherSuite c) {
    switch (c) {
        case 0xC02B: return "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
        case 0xC02C: return "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
        case 0xC02F: return "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256";
        case 0xC030: return "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384";
        case 0xCCA8: return "TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256";
        case 0xCCA9: return "TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256";
        case 0xC009: return "TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA";
        case 0xC00A: return "TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA";
        case 0xC013: return "TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA";
        case 0xC014: return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA";
        case 0x009C: return "TLS_RSA_WITH_AES_128_GCM_SHA256";
        case 0x009D: return "TLS_RSA_WITH_AES_256_GCM_SHA384";
        case 0x002F: return "TLS_RSA_WITH_AES_128_CBC_SHA";
        case 0x0035: return "TLS_RSA_WITH_AES_256_CBC_SHA";
        default: return nullptr;
    }
}

}  // namespace

TlsSession* TlsCreate(int fd, const TlsOptions& options) {
    auto* s = new TlsSession();
    s->fd = fd;
    s->ctx = SSLCreateContext(kCFAllocatorDefault, kSSLClientSide, kSSLStreamType);
    if (s->ctx == nullptr) {
        delete s;
        return nullptr;
    }
    SSLSetIOFuncs(s->ctx, ReadCallback, WriteCallback);
    SSLSetConnection(s->ctx, s);
    SSLSetProtocolVersionMin(s->ctx, kTLSProtocol12);
    // Stop at the server's certificate so SecureTransport does not judge it; the caller does.
    SSLSetSessionOption(s->ctx, kSSLSessionOptionBreakOnServerAuth, true);
    if (!options.hostname.empty()) {
        SSLSetPeerDomainName(s->ctx, options.hostname.c_str(), options.hostname.size());
    }
    if (!options.alpn.empty()) {
        CFMutableArrayRef list = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        for (const std::string& p : options.alpn) {
            CFStringRef name = CFStringCreateWithCString(kCFAllocatorDefault, p.c_str(), kCFStringEncodingUTF8);
            if (name != nullptr) {
                CFArrayAppendValue(list, name);
                CFRelease(name);
            }
        }
        SSLSetALPNProtocols(s->ctx, list);
        CFRelease(list);
    }
    std::lock_guard<std::mutex> lock(g_liveMu);
    g_live.insert(s);
    return s;
}

bool TlsIsLive(TlsSession* session) {
    std::lock_guard<std::mutex> lock(g_liveMu);
    return g_live.count(session) != 0;
}

int TlsHandshake(TlsSession* s, int timeoutMs) {
    Use use(s);
    if (!use.ok) return -EBADF;
    int failure = 0;
    for (;;) {
        const OSStatus st = Drive(s, timeoutMs, [&] { return SSLHandshake(s->ctx); }, &failure);
        if (failure != 0) return failure;
        if (st == noErr) {
            CaptureChain(s);
            CFArrayRef alpn = nullptr;
            if (SSLCopyALPNProtocols(s->ctx, &alpn) == noErr && alpn != nullptr) {
                if (CFArrayGetCount(alpn) > 0) {
                    auto name = static_cast<CFStringRef>(CFArrayGetValueAtIndex(alpn, 0));
                    char buf[256];
                    if (CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8)) s->alpn = buf;
                }
                CFRelease(alpn);
            }
            return 0;
        }
        if (st == errSSLServerAuthCompleted) {
            CaptureChain(s);
            continue;  // resume: the caller verifies the chain after the handshake
        }
        s->lastError = StatusText(st);
        return kTlsHandshakeFailed;
    }
}

int TlsRead(TlsSession* s, uint8_t* buf, int len, int timeoutMs) {
    if (len <= 0) return 0;
    Use use(s);
    if (!use.ok) return -EBADF;
    int failure = 0;
    size_t processed = 0;
    const OSStatus st = Drive(s, timeoutMs, [&] {
        processed = 0;
        OSStatus r = SSLRead(s->ctx, buf, static_cast<size_t>(len), &processed);
        // Bytes read before the transport ran dry are a successful short read.
        if (r == errSSLWouldBlock && processed > 0) return static_cast<OSStatus>(noErr);
        return r;
    }, &failure);
    if (st == noErr && processed > 0) return static_cast<int>(processed);
    if (failure != 0) return failure;
    if (st == errSSLClosedGraceful || st == errSSLClosedNoNotify || s->peerEof) return 0;
    s->lastError = StatusText(st);
    return st == errSSLClosedAbort ? -ECONNRESET : kTlsProtocolError;
}

int TlsWrite(TlsSession* s, const uint8_t* buf, int len, int timeoutMs) {
    Use use(s);
    if (!use.ok) return -EBADF;
    int done = 0;
    std::lock_guard<std::mutex> lock(s->mu);
    s->writeTimeoutMs = timeoutMs > 0 ? timeoutMs : 60000;
    while (done < len) {
        size_t processed = 0;
        const OSStatus st = SSLWrite(s->ctx, buf + done, static_cast<size_t>(len - done), &processed);
        done += static_cast<int>(processed);
        if (st != noErr) {
            s->lastError = StatusText(st);
            if (st == errSSLWouldBlock) return -ETIMEDOUT;
            return s->peerEof ? -EPIPE : (st == errSSLClosedAbort ? -ECONNRESET : kTlsProtocolError);
        }
        if (processed == 0) break;
    }
    return done >= len ? len : kTlsProtocolError;
}

int TlsAvailable(TlsSession* s) {
    Use use(s);
    if (!use.ok) return 0;
    std::lock_guard<std::mutex> lock(s->mu);
    size_t n = 0;
    return SSLGetBufferedReadSize(s->ctx, &n) == noErr ? static_cast<int>(n) : 0;
}

void TlsShutdown(TlsSession* s) {
    Use use(s);
    if (!use.ok) return;
    std::lock_guard<std::mutex> lock(s->mu);
    SSLClose(s->ctx);
}

void TlsDestroy(TlsSession* s) {
    {
        std::lock_guard<std::mutex> lock(g_liveMu);
        if (g_live.erase(s) == 0) return;
    }
    while (s->users.load() != 0) std::this_thread::yield();
    CFRelease(s->ctx);
    delete s;
}

std::vector<std::vector<uint8_t>> TlsPeerChain(TlsSession* s) {
    Use use(s);
    if (!use.ok) return {};
    std::lock_guard<std::mutex> lock(s->mu);
    return s->chain;
}

std::string TlsProtocol(TlsSession* s) {
    Use use(s);
    if (!use.ok) return "";
    std::lock_guard<std::mutex> lock(s->mu);
    SSLProtocol v = kSSLProtocolUnknown;
    if (SSLGetNegotiatedProtocolVersion(s->ctx, &v) != noErr) return "";
    switch (v) {
        case kTLSProtocol13: return "TLSv1.3";
        case kTLSProtocol12: return "TLSv1.2";
        case kTLSProtocol11: return "TLSv1.1";
        case kTLSProtocol1: return "TLSv1";
        default: return "";
    }
}

std::string TlsCipher(TlsSession* s) {
    Use use(s);
    if (!use.ok) return "";
    std::lock_guard<std::mutex> lock(s->mu);
    SSLCipherSuite c = 0;
    if (SSLGetNegotiatedCipher(s->ctx, &c) != noErr) return "";
    if (const char* name = CipherName(c)) return name;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "TLS_UNKNOWN_0x%04X", static_cast<unsigned>(c));
    return buf;
}

std::string TlsAlpn(TlsSession* s) {
    Use use(s);
    return use.ok ? s->alpn : std::string();
}

std::string TlsLastError(TlsSession* s) {
    Use use(s);
    return use.ok ? s->lastError : std::string();
}

int TlsEvaluateChain(const std::vector<std::vector<uint8_t>>& chain,
                     const std::vector<std::vector<uint8_t>>& anchors, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error != nullptr) *error = why;
        return 1;
    };
    if (chain.empty()) return fail("empty certificate chain");

    auto makeCerts = [](const std::vector<std::vector<uint8_t>>& ders) -> CFMutableArrayRef {
        CFMutableArrayRef list = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        for (const auto& der : ders) {
            CFDataRef data = CFDataCreate(kCFAllocatorDefault, der.data(), static_cast<CFIndex>(der.size()));
            SecCertificateRef cert = data != nullptr ? SecCertificateCreateWithData(kCFAllocatorDefault, data) : nullptr;
            if (data != nullptr) CFRelease(data);
            if (cert != nullptr) {
                CFArrayAppendValue(list, cert);
                CFRelease(cert);
            }
        }
        return list;
    };

    CFMutableArrayRef certs = makeCerts(chain);
    if (CFArrayGetCount(certs) == 0) {
        CFRelease(certs);
        return fail("malformed certificate");
    }
    SecPolicyRef policy = SecPolicyCreateSSL(true, nullptr);  // host name is checked by the caller
    SecTrustRef trust = nullptr;
    OSStatus st = SecTrustCreateWithCertificates(certs, policy, &trust);
    int rc = 1;
    std::string why = "unable to verify the certificate chain";
    if (st == noErr && trust != nullptr) {
        CFMutableArrayRef anchorCerts = nullptr;
        if (!anchors.empty()) {
            anchorCerts = makeCerts(anchors);
            SecTrustSetAnchorCertificates(trust, anchorCerts);
            SecTrustSetAnchorCertificatesOnly(trust, true);
        }
        CFErrorRef cfError = nullptr;
        if (SecTrustEvaluateWithError(trust, &cfError)) {
            rc = 0;
        } else if (cfError != nullptr) {
            CFStringRef text = CFErrorCopyDescription(cfError);
            char buf[512];
            if (text != nullptr && CFStringGetCString(text, buf, sizeof(buf), kCFStringEncodingUTF8)) why = buf;
            if (text != nullptr) CFRelease(text);
            CFRelease(cfError);
        }
        if (anchorCerts != nullptr) CFRelease(anchorCerts);
    } else {
        why = StatusText(st);
    }
    if (trust != nullptr) CFRelease(trust);
    CFRelease(policy);
    CFRelease(certs);
    if (rc != 0 && error != nullptr) *error = why;
    return rc;
}

}  // namespace kudroid
