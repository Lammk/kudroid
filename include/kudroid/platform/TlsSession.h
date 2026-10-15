// Client-side TLS over a connected socket, for java.net / javax.net.ssl.
//
// The handshake does not judge the server's certificate. The peer chain is handed to the
// caller, which asks the application's TrustManager (or TlsEvaluateChain) whether to trust
// it, then checks the host name itself. That keeps trust policy where Java expects it and
// leaves this layer to move bytes.
//
// All blocking calls take a timeout in milliseconds (<= 0 waits forever) and return a
// negative errno-style code on failure. Reads and writes may run on different threads.
#ifndef KUDROID_PLATFORM_TLSSESSION_H
#define KUDROID_PLATFORM_TLSSESSION_H

#include <stdint.h>

#include <string>
#include <vector>

namespace kudroid {

class TlsSession;

struct TlsOptions {
    std::string hostname;             // server name indication; empty for none
    std::vector<std::string> alpn;    // offered application protocols
    int minVersion = 0;               // 0 default; 12 = TLS 1.2, 13 = TLS 1.3
};

// Failure codes beyond errno values.
constexpr int kTlsHandshakeFailed = -2000;
constexpr int kTlsProtocolError = -2001;
constexpr int kTlsUnsupported = -2002;

// A session over `fd`, which stays owned by the caller. Null when TLS is unavailable.
TlsSession* TlsCreate(int fd, const TlsOptions& options);
int TlsHandshake(TlsSession* session, int timeoutMs);
// Bytes read, 0 at end of stream, or a negative code (-110 on timeout).
int TlsRead(TlsSession* session, uint8_t* buf, int len, int timeoutMs);
// Writes all of `buf`: len on success.
int TlsWrite(TlsSession* session, const uint8_t* buf, int len, int timeoutMs);
// Decrypted bytes ready without touching the network.
int TlsAvailable(TlsSession* session);
void TlsShutdown(TlsSession* session);
void TlsDestroy(TlsSession* session);
// Whether `session` is still a live handle (a destroyed one is rejected, not dereferenced).
bool TlsIsLive(TlsSession* session);

// The server's certificates, leaf first, DER encoded.
std::vector<std::vector<uint8_t>> TlsPeerChain(TlsSession* session);
std::string TlsProtocol(TlsSession* session);  // "TLSv1.2"
std::string TlsCipher(TlsSession* session);    // IANA suite name
std::string TlsAlpn(TlsSession* session);      // negotiated protocol, or empty
std::string TlsLastError(TlsSession* session);

// Whether `chain` (leaf first, DER) leads to a trusted root. `anchors` replaces the platform
// roots when non-empty. The host name is not checked here. 0 when trusted, else a code with
// the reason in `*error`.
int TlsEvaluateChain(const std::vector<std::vector<uint8_t>>& chain,
                     const std::vector<std::vector<uint8_t>>& anchors, std::string* error);

}  // namespace kudroid

#endif
