// TLS over memory BIOs: the socket is only ever touched non-blocking, and waiting happens in
// poll() outside the session lock, so one thread can sit in a read while another writes.
#include "kudroid/platform/TlsSession.h"

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <set>
#include <thread>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

namespace kudroid {

class TlsSession {
public:
    int fd = -1;
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    BIO* rbio = nullptr;
    BIO* wbio = nullptr;
    std::mutex mu;
    std::vector<uint8_t> pendingOut;  // encrypted bytes the socket has not taken yet
    bool peerEof = false;
    std::atomic<int> users{0};  // calls currently inside the session
    std::string lastError;
};

namespace {

std::mutex g_liveMu;
std::set<TlsSession*> g_live;

// Pins a session for one call: a destroyed handle fails the call instead of being dereferenced,
// and destruction waits for the call to finish.
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

std::string DrainErrors() {
    std::string text;
    unsigned long e;
    while ((e = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(e, buf, sizeof(buf));
        if (!text.empty()) text += "; ";
        text += buf;
    }
    return text;
}

// Moves encrypted output from the BIO to the socket. False when the socket is full.
bool FlushLocked(TlsSession* s) {
    uint8_t tmp[16384];
    for (;;) {
        if (s->pendingOut.empty()) {
            int n = BIO_read(s->wbio, tmp, sizeof(tmp));
            if (n <= 0) return true;
            s->pendingOut.assign(tmp, tmp + n);
        }
        const ssize_t sent = ::send(s->fd, s->pendingOut.data(), s->pendingOut.size(),
                                    MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent > 0) {
            s->pendingOut.erase(s->pendingOut.begin(), s->pendingOut.begin() + sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        return false;  // would block, or the connection is gone: the next wait sorts out which
    }
}

// Moves what the socket has into the BIO. Returns false once the peer closed.
void FillLocked(TlsSession* s) {
    uint8_t tmp[16384];
    for (;;) {
        const ssize_t n = ::recv(s->fd, tmp, sizeof(tmp), MSG_DONTWAIT);
        if (n > 0) {
            BIO_write(s->rbio, tmp, static_cast<int>(n));
            if (static_cast<size_t>(n) < sizeof(tmp)) return;
            continue;
        }
        if (n == 0) {
            s->peerEof = true;
            return;
        }
        if (errno == EINTR) continue;
        return;
    }
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

// Runs one SSL call to completion, pumping the socket whenever OpenSSL needs it.
// op() returns the SSL call's result and sets *err to SSL_get_error. Returns the final
// result of op, or a negative code via *failure.
template <typename Op>
int Drive(TlsSession* s, int timeoutMs, Op op, int* sslError, int* failure) {
    const bool bounded = timeoutMs > 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(bounded ? timeoutMs : 0);
    *failure = 0;
    for (;;) {
        short wait = 0;
        int ret;
        {
            std::lock_guard<std::mutex> lock(s->mu);
            ERR_clear_error();
            ret = op(sslError);
            const bool flushed = FlushLocked(s);
            if (ret > 0 || (*sslError != SSL_ERROR_WANT_READ && *sslError != SSL_ERROR_WANT_WRITE)) {
                return ret;  // any output still queued goes out with the next call
            }
            if (!flushed) {
                wait = POLLOUT;
            } else if (*sslError == SSL_ERROR_WANT_READ) {
                if (s->peerEof) {
                    *sslError = SSL_ERROR_SYSCALL;
                    return ret;
                }
                wait = POLLIN;
            } else {
                wait = POLLOUT;
            }
        }
        const int ready = WaitFd(s->fd, wait, deadline, bounded);
        if (ready == 0) {
            *failure = -ETIMEDOUT;
            return -1;
        }
        if (ready < 0) {
            *failure = ready;
            return -1;
        }
        if (wait == POLLIN) {
            std::lock_guard<std::mutex> lock(s->mu);
            FillLocked(s);
        }
    }
}

std::string VersionName(int v) {
    switch (v) {
        case TLS1_3_VERSION: return "TLSv1.3";
        case TLS1_2_VERSION: return "TLSv1.2";
        case TLS1_1_VERSION: return "TLSv1.1";
        case TLS1_VERSION: return "TLSv1";
        default: return "";
    }
}

}  // namespace

TlsSession* TlsCreate(int fd, const TlsOptions& options) {
    auto* s = new TlsSession();
    s->fd = fd;
    s->ctx = SSL_CTX_new(TLS_client_method());
    if (s->ctx == nullptr) {
        delete s;
        return nullptr;
    }
    SSL_CTX_set_verify(s->ctx, SSL_VERIFY_NONE, nullptr);
    SSL_CTX_set_mode(s->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    SSL_CTX_set_min_proto_version(s->ctx, options.minVersion >= 13 ? TLS1_3_VERSION : TLS1_2_VERSION);
    if (!options.alpn.empty()) {
        std::vector<uint8_t> wire;
        for (const std::string& p : options.alpn) {
            if (p.empty() || p.size() > 255) continue;
            wire.push_back(static_cast<uint8_t>(p.size()));
            wire.insert(wire.end(), p.begin(), p.end());
        }
        SSL_CTX_set_alpn_protos(s->ctx, wire.data(), static_cast<unsigned>(wire.size()));
    }
    s->ssl = SSL_new(s->ctx);
    s->rbio = BIO_new(BIO_s_mem());
    s->wbio = BIO_new(BIO_s_mem());
    if (s->ssl == nullptr || s->rbio == nullptr || s->wbio == nullptr) {
        if (s->ssl != nullptr) SSL_free(s->ssl);
        if (s->rbio != nullptr) BIO_free(s->rbio);
        if (s->wbio != nullptr) BIO_free(s->wbio);
        SSL_CTX_free(s->ctx);
        delete s;
        return nullptr;
    }
    SSL_set_bio(s->ssl, s->rbio, s->wbio);  // the SSL now owns both
    if (!options.hostname.empty()) SSL_set_tlsext_host_name(s->ssl, options.hostname.c_str());
    SSL_set_connect_state(s->ssl);
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
    int sslError = 0;
    int failure = 0;
    const int ret = Drive(s, timeoutMs, [&](int* err) {
        const int r = SSL_do_handshake(s->ssl);
        *err = r > 0 ? SSL_ERROR_NONE : SSL_get_error(s->ssl, r);
        return r;
    }, &sslError, &failure);
    if (ret == 1) return 0;
    if (failure != 0) return failure;
    s->lastError = DrainErrors();
    if (s->lastError.empty()) s->lastError = s->peerEof ? "connection closed during handshake" : "handshake failed";
    return kTlsHandshakeFailed;
}

int TlsRead(TlsSession* s, uint8_t* buf, int len, int timeoutMs) {
    if (len <= 0) return 0;
    Use use(s);
    if (!use.ok) return -EBADF;
    int sslError = 0;
    int failure = 0;
    const int ret = Drive(s, timeoutMs, [&](int* err) {
        const int r = SSL_read(s->ssl, buf, len);
        *err = r > 0 ? SSL_ERROR_NONE : SSL_get_error(s->ssl, r);
        return r;
    }, &sslError, &failure);
    if (ret > 0) return ret;
    if (failure != 0) return failure;
    if (sslError == SSL_ERROR_ZERO_RETURN) return 0;
    if (s->peerEof) return 0;  // closed without close_notify
    s->lastError = DrainErrors();
    return kTlsProtocolError;
}

int TlsWrite(TlsSession* s, const uint8_t* buf, int len, int timeoutMs) {
    Use use(s);
    if (!use.ok) return -EBADF;
    int done = 0;
    while (done < len) {
        int sslError = 0;
        int failure = 0;
        const int ret = Drive(s, timeoutMs, [&](int* err) {
            const int r = SSL_write(s->ssl, buf + done, len - done);
            *err = r > 0 ? SSL_ERROR_NONE : SSL_get_error(s->ssl, r);
            return r;
        }, &sslError, &failure);
        if (ret > 0) {
            done += ret;
            continue;
        }
        if (failure != 0) return failure;
        s->lastError = DrainErrors();
        return s->peerEof ? -EPIPE : kTlsProtocolError;
    }
    // Make sure the tail has left for the socket.
    for (;;) {
        std::lock_guard<std::mutex> lock(s->mu);
        if (FlushLocked(s)) break;
        // Socket full: wait outside the lock on the next iteration is overkill for the tail;
        // a short blocking wait is enough.
        struct pollfd p = {s->fd, POLLOUT, 0};
        ::poll(&p, 1, 50);
    }
    return len;
}

int TlsAvailable(TlsSession* s) {
    Use use(s);
    if (!use.ok) return 0;
    std::lock_guard<std::mutex> lock(s->mu);
    return SSL_pending(s->ssl);
}

void TlsShutdown(TlsSession* s) {
    Use use(s);
    if (!use.ok) return;
    std::lock_guard<std::mutex> lock(s->mu);
    if (SSL_is_init_finished(s->ssl)) {
        SSL_shutdown(s->ssl);
        FlushLocked(s);
    }
}

void TlsDestroy(TlsSession* s) {
    {
        std::lock_guard<std::mutex> lock(g_liveMu);
        if (g_live.erase(s) == 0) return;
    }
    // New calls are refused now; let the ones already inside finish.
    while (s->users.load() != 0) std::this_thread::yield();
    SSL_free(s->ssl);  // frees the BIOs
    SSL_CTX_free(s->ctx);
    delete s;
}

std::vector<std::vector<uint8_t>> TlsPeerChain(TlsSession* s) {
    std::vector<std::vector<uint8_t>> out;
    Use use(s);
    if (!use.ok) return out;
    std::lock_guard<std::mutex> lock(s->mu);
    STACK_OF(X509)* chain = SSL_get_peer_cert_chain(s->ssl);
    if (chain == nullptr) return out;
    for (int i = 0; i < sk_X509_num(chain); ++i) {
        X509* cert = sk_X509_value(chain, i);
        const int n = i2d_X509(cert, nullptr);
        if (n <= 0) continue;
        std::vector<uint8_t> der(static_cast<size_t>(n));
        uint8_t* p = der.data();
        i2d_X509(cert, &p);
        out.push_back(std::move(der));
    }
    return out;
}

std::string TlsProtocol(TlsSession* s) {
    Use use(s);
    if (!use.ok) return "";
    std::lock_guard<std::mutex> lock(s->mu);
    return VersionName(SSL_version(s->ssl));
}

std::string TlsCipher(TlsSession* s) {
    Use use(s);
    if (!use.ok) return "";
    std::lock_guard<std::mutex> lock(s->mu);
    const SSL_CIPHER* c = SSL_get_current_cipher(s->ssl);
    const char* name = c != nullptr ? SSL_CIPHER_standard_name(c) : nullptr;
    return name != nullptr ? name : "";
}

std::string TlsAlpn(TlsSession* s) {
    Use use(s);
    if (!use.ok) return "";
    std::lock_guard<std::mutex> lock(s->mu);
    const unsigned char* data = nullptr;
    unsigned len = 0;
    SSL_get0_alpn_selected(s->ssl, &data, &len);
    return data != nullptr ? std::string(reinterpret_cast<const char*>(data), len) : std::string();
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
    X509_STORE* store = X509_STORE_new();
    X509_STORE_CTX* sctx = X509_STORE_CTX_new();
    STACK_OF(X509)* untrusted = sk_X509_new_null();
    std::vector<X509*> owned;
    auto parse = [&](const std::vector<uint8_t>& der) -> X509* {
        const uint8_t* p = der.data();
        return d2i_X509(nullptr, &p, static_cast<long>(der.size()));
    };
    int rc = 1;
    std::string why = "unable to verify the certificate chain";
    X509* leaf = parse(chain[0]);
    if (leaf != nullptr) {
        owned.push_back(leaf);
        for (size_t i = 1; i < chain.size(); ++i) {
            X509* c = parse(chain[i]);
            if (c != nullptr) {
                owned.push_back(c);
                sk_X509_push(untrusted, c);
            }
        }
        if (!anchors.empty()) {
            for (const auto& a : anchors) {
                X509* c = parse(a);
                if (c != nullptr) {
                    X509_STORE_add_cert(store, c);
                    X509_free(c);
                }
            }
        } else {
            const char* caFile = getenv("KUDROID_TLS_CAFILE");
            if (caFile != nullptr) X509_STORE_load_file(store, caFile);
            else X509_STORE_set_default_paths(store);
        }
        if (X509_STORE_CTX_init(sctx, store, leaf, untrusted) == 1) {
            X509_STORE_CTX_set_purpose(sctx, X509_PURPOSE_SSL_SERVER);
            if (X509_verify_cert(sctx) == 1) {
                rc = 0;
            } else {
                why = X509_verify_cert_error_string(X509_STORE_CTX_get_error(sctx));
            }
        }
    } else {
        why = "malformed certificate";
    }
    for (X509* c : owned) X509_free(c);
    sk_X509_free(untrusted);
    X509_STORE_CTX_free(sctx);
    X509_STORE_free(store);
    if (rc != 0 && error != nullptr) *error = why;
    return rc;
}

}  // namespace kudroid
