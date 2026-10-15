// libcore.io.KuTls: TLS sessions for javax.net.ssl, over the platform's TLS backend.
//
// A session is addressed by an opaque handle. Certificate chains cross the boundary as a run
// of {4-byte big-endian length, DER bytes} records.
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/DexValue.h"
#include "kudroid/kuart/LibCore.h"
#include "kudroid/kuart/VmLock.h"
#include "kudroid/platform/TlsSession.h"

namespace kudroid {
namespace kuart {

namespace {

constexpr int kEinval = 22;
constexpr int kMaxChunk = 256 * 1024;

const char* Utf8(const DexValue& v) {
    if (v.l == nullptr) return "";
    auto* str = reinterpret_cast<DexString*>(v.l);
    return str->utf8 ? str->utf8 : "";
}

DexArray* Bytes(const DexValue& v) { return reinterpret_cast<DexArray*>(v.l); }

bool RangeOk(const DexArray* a, int off, int len) {
    return a != nullptr && off >= 0 && len >= 0 && off <= a->length && len <= a->length - off;
}

TlsSession* Handle(const DexValue& v) { return reinterpret_cast<TlsSession*>(v.j); }

std::vector<std::string> SplitCsv(const std::string& csv) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= csv.size()) {
        const size_t comma = csv.find(',', start);
        const size_t end = comma == std::string::npos ? csv.size() : comma;
        if (end > start) out.push_back(csv.substr(start, end - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

std::vector<uint8_t> Pack(const std::vector<std::vector<uint8_t>>& chain) {
    std::vector<uint8_t> out;
    for (const auto& der : chain) {
        const uint32_t n = static_cast<uint32_t>(der.size());
        out.push_back(static_cast<uint8_t>(n >> 24));
        out.push_back(static_cast<uint8_t>(n >> 16));
        out.push_back(static_cast<uint8_t>(n >> 8));
        out.push_back(static_cast<uint8_t>(n));
        out.insert(out.end(), der.begin(), der.end());
    }
    return out;
}

std::vector<std::vector<uint8_t>> Unpack(const uint8_t* data, int len) {
    std::vector<std::vector<uint8_t>> out;
    int pos = 0;
    while (pos + 4 <= len) {
        const uint32_t n = (static_cast<uint32_t>(data[pos]) << 24) |
                           (static_cast<uint32_t>(data[pos + 1]) << 16) |
                           (static_cast<uint32_t>(data[pos + 2]) << 8) |
                           static_cast<uint32_t>(data[pos + 3]);
        pos += 4;
        if (n > static_cast<uint32_t>(len - pos)) break;
        out.emplace_back(data + pos, data + pos + n);
        pos += static_cast<int>(n);
    }
    return out;
}

// Copies text into out; returns its length, or 0 when it does not fit.
int CopyText(const std::string& text, DexArray* out) {
    if (out == nullptr || static_cast<int>(text.size()) > out->length) return 0;
    std::memcpy(out->Data(), text.data(), text.size());
    return static_cast<int>(text.size());
}

}  // namespace

bool Invoke_libcore_io_KuTls(Interpreter* /*interp*/, const char* name, const DexValue* args,
                             size_t num_args, DexValue* result) {
    const auto Is = [&](const char* n) { return std::strcmp(name, n) == 0; };

    if (Is("create") && num_args >= 4) {
        TlsOptions options;
        options.hostname = Utf8(args[1]);
        options.alpn = SplitCsv(Utf8(args[2]));
        options.minVersion = args[3].i;
        TlsSession* s = TlsCreate(args[0].i, options);
        *result = DexValue::Long(static_cast<int64_t>(reinterpret_cast<intptr_t>(s)));
        return true;
    }
    if (Is("handshake") && num_args >= 2) {
        TlsSession* s = Handle(args[0]);
        int rc;
        {
            VmLockRelease unlocked;
            rc = TlsHandshake(s, args[1].i);
        }
        *result = DexValue::Int(rc);
        return true;
    }
    if (Is("read") && num_args >= 5) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i;
        const int len = args[3].i;
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        if (len == 0) {
            *result = DexValue::Int(0);
            return true;
        }
        std::vector<uint8_t> tmp(static_cast<size_t>(std::min(len, kMaxChunk)));
        int n;
        {
            VmLockRelease unlocked;
            n = TlsRead(Handle(args[0]), tmp.data(), static_cast<int>(tmp.size()), args[4].i);
        }
        if (n > 0) std::memcpy(buf->Data() + off, tmp.data(), static_cast<size_t>(n));
        *result = DexValue::Int(n);
        return true;
    }
    if (Is("write") && num_args >= 5) {
        DexArray* buf = Bytes(args[1]);
        const int off = args[2].i;
        const int len = args[3].i;
        if (!RangeOk(buf, off, len)) {
            *result = DexValue::Int(-kEinval);
            return true;
        }
        std::vector<uint8_t> tmp(buf->Data() + off, buf->Data() + off + len);
        int n;
        {
            VmLockRelease unlocked;
            n = TlsWrite(Handle(args[0]), tmp.data(), len, args[4].i);
        }
        *result = DexValue::Int(n);
        return true;
    }
    if (Is("available") && num_args >= 1) {
        *result = DexValue::Int(TlsAvailable(Handle(args[0])));
        return true;
    }
    if (Is("shutdown") && num_args >= 1) {
        VmLockRelease unlocked;
        TlsShutdown(Handle(args[0]));
        return true;
    }
    if (Is("destroy") && num_args >= 1) {
        VmLockRelease unlocked;
        TlsDestroy(Handle(args[0]));
        return true;
    }
    if (Is("peerChain") && num_args >= 2) {
        // Returns the packed size; the bytes are copied only when `out` is large enough.
        const std::vector<uint8_t> packed = Pack(TlsPeerChain(Handle(args[0])));
        DexArray* out = Bytes(args[1]);
        if (out != nullptr && static_cast<int>(packed.size()) <= out->length) {
            std::memcpy(out->Data(), packed.data(), packed.size());
        }
        *result = DexValue::Int(static_cast<int>(packed.size()));
        return true;
    }
    if (Is("info") && num_args >= 3) {
        TlsSession* s = Handle(args[0]);
        std::string text;
        switch (args[1].i) {
            case 0: text = TlsProtocol(s); break;
            case 1: text = TlsCipher(s); break;
            case 2: text = TlsAlpn(s); break;
            default: text = TlsLastError(s); break;
        }
        *result = DexValue::Int(CopyText(text, Bytes(args[2])));
        return true;
    }
    if (Is("evaluate") && num_args >= 5) {
        // (chain, chainLen, anchors, anchorsLen, errOut): 0 when trusted, else 1 + error length.
        DexArray* chain = Bytes(args[0]);
        DexArray* anchors = Bytes(args[2]);
        const int chainLen = args[1].i;
        const int anchorsLen = args[3].i;
        if ((chain == nullptr || chainLen < 0 || chainLen > chain->length) ||
            (anchors != nullptr && (anchorsLen < 0 || anchorsLen > anchors->length))) {
            *result = DexValue::Int(1);
            return true;
        }
        const auto chainDers = Unpack(chain->Data(), chainLen);
        const auto anchorDers = anchors != nullptr ? Unpack(anchors->Data(), anchorsLen)
                                                   : std::vector<std::vector<uint8_t>>();
        std::string error;
        int rc;
        {
            VmLockRelease unlocked;
            rc = TlsEvaluateChain(chainDers, anchorDers, &error);
        }
        *result = DexValue::Int(rc == 0 ? 0 : 1 + CopyText(error, Bytes(args[4])));
        return true;
    }
    return false;
}

}  // namespace kuart
}  // namespace kudroid
