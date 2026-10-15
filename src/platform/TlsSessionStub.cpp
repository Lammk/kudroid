// No TLS backend on this host: sessions cannot be created, and nothing is trusted.
#include "kudroid/platform/TlsSession.h"

namespace kudroid {

TlsSession* TlsCreate(int, const TlsOptions&) { return nullptr; }
int TlsHandshake(TlsSession*, int) { return kTlsUnsupported; }
int TlsRead(TlsSession*, uint8_t*, int, int) { return kTlsUnsupported; }
int TlsWrite(TlsSession*, const uint8_t*, int, int) { return kTlsUnsupported; }
int TlsAvailable(TlsSession*) { return 0; }
void TlsShutdown(TlsSession*) {}
void TlsDestroy(TlsSession*) {}
bool TlsIsLive(TlsSession*) { return false; }
std::vector<std::vector<uint8_t>> TlsPeerChain(TlsSession*) { return {}; }
std::string TlsProtocol(TlsSession*) { return ""; }
std::string TlsCipher(TlsSession*) { return ""; }
std::string TlsAlpn(TlsSession*) { return ""; }
std::string TlsLastError(TlsSession*) { return "TLS is not available"; }
int TlsEvaluateChain(const std::vector<std::vector<uint8_t>>&,
                     const std::vector<std::vector<uint8_t>>&, std::string* error) {
    if (error != nullptr) *error = "TLS is not available";
    return 1;
}

}  // namespace kudroid
