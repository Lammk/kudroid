#include "kudroid/platform/RsaSignature.h"

#include <cstring>
#include <limits>
#include <memory>

#if defined(__APPLE__)
#include <Security/Security.h>
#elif defined(KUDROID_HAS_OPENSSL)
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#endif

namespace kudroid {
namespace {

struct Bytes {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct DerReader {
    Bytes bytes;
    bool Read(uint8_t tag, Bytes* out) {
        if (bytes.size < 2 || bytes.data[0] != tag) return false;
        size_t head = 2, length = bytes.data[1];
        if (length & 0x80) {
            const size_t count = length & 0x7f;
            if (!count || count > sizeof(size_t) || count > bytes.size - head || !bytes.data[head])
                return false;
            length = 0;
            for (size_t i = 0; i < count; ++i) length = (length << 8) | bytes.data[head++];
            if (length < 128) return false;
        }
        if (length > bytes.size - head) return false;
        *out = {bytes.data + head, length};
        bytes.data += head + length;
        bytes.size -= head + length;
        return true;
    }
};

bool PositiveInteger(DerReader* reader) {
    Bytes integer;
    if (!reader->Read(0x02, &integer) || !integer.size || (integer.data[0] & 0x80)) return false;
    if (integer.data[0] == 0 && (integer.size == 1 || !(integer.data[1] & 0x80))) return false;
    return true;
}

bool RsaPkcs1(Bytes spki, Bytes* pkcs1) {
    if (!spki.data) return false;
    DerReader outer{spki};
    Bytes sequence, algorithm, oid, bits, parameters;
    if (!outer.Read(0x30, &sequence) || outer.bytes.size) return false;
    DerReader contents{sequence};
    if (!contents.Read(0x30, &algorithm) || !contents.Read(0x03, &bits) || contents.bytes.size)
        return false;
    DerReader identifier{algorithm};
    static const uint8_t rsa_oid[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};
    if (!identifier.Read(0x06, &oid) || oid.size != sizeof(rsa_oid) ||
        std::memcmp(oid.data, rsa_oid, sizeof(rsa_oid))) return false;
    if (identifier.bytes.size &&
        (!identifier.Read(0x05, &parameters) || parameters.size || identifier.bytes.size)) return false;
    if (bits.size < 2 || bits.data[0] != 0) return false;
    *pkcs1 = {bits.data + 1, bits.size - 1};
    DerReader key{*pkcs1};
    if (!key.Read(0x30, &sequence) || key.bytes.size) return false;
    DerReader integers{sequence};
    return PositiveInteger(&integers) && PositiveInteger(&integers) && !integers.bytes.size;
}

#if defined(__APPLE__)
template<class T> struct Cf {
    T value = nullptr;
    explicit Cf(T v = nullptr) : value(v) {}
    ~Cf() { if (value) CFRelease(value); }
    Cf(const Cf&) = delete;
    Cf& operator=(const Cf&) = delete;
};

SecKeyRef AppleKey(Bytes pkcs1) {
    Cf<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, pkcs1.data, pkcs1.size));
    const void* keys[] = {kSecAttrKeyType, kSecAttrKeyClass};
    const void* values[] = {kSecAttrKeyTypeRSA, kSecAttrKeyClassPublic};
    Cf<CFDictionaryRef> attrs(CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
                                               &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
    if (!data.value || !attrs.value) return nullptr;
    Cf<CFErrorRef> error;
    return SecKeyCreateWithData(data.value, attrs.value, &error.value);
}

SecKeyAlgorithm Algorithm(const char* name) {
    if (!name) return nullptr;
    if (!std::strcmp(name, "SHA224withRSA")) return kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA224;
    if (!std::strcmp(name, "SHA256withRSA")) return kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256;
    if (!std::strcmp(name, "SHA384withRSA")) return kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA384;
    if (!std::strcmp(name, "SHA512withRSA")) return kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA512;
    return nullptr;
}
#elif defined(KUDROID_HAS_OPENSSL)
struct ErrorScope {
    bool marked = ERR_set_mark() != 0;
    ~ErrorScope() { if (marked) ERR_pop_to_mark(); else ERR_clear_error(); }
};
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
Key OpenSslKey(Bytes der) {
    const auto* p = der.data;
    Key key(d2i_PUBKEY(nullptr, &p, static_cast<long>(der.size)), EVP_PKEY_free);
    if (!key || p != der.data + der.size || EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA) return {nullptr, EVP_PKEY_free};
    return key;
}
const EVP_MD* Algorithm(const char* name) {
    if (!name) return nullptr;
    if (!std::strcmp(name, "SHA224withRSA")) return EVP_sha224();
    if (!std::strcmp(name, "SHA256withRSA")) return EVP_sha256();
    if (!std::strcmp(name, "SHA384withRSA")) return EVP_sha384();
    if (!std::strcmp(name, "SHA512withRSA")) return EVP_sha512();
    return nullptr;
}
#endif
}  // namespace

bool RsaAvailable() {
#if defined(__APPLE__) || defined(KUDROID_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}

bool ValidateRsaSpki(const uint8_t* der, size_t length) {
    Bytes pkcs1;
    if (length > static_cast<size_t>(std::numeric_limits<long>::max()) || !RsaPkcs1({der, length}, &pkcs1))
        return false;
#if defined(__APPLE__)
    Cf<SecKeyRef> key(AppleKey(pkcs1));
    return key.value != nullptr;
#elif defined(KUDROID_HAS_OPENSSL)
    ErrorScope errors;
    return bool(OpenSslKey({der, length}));
#else
    return false;
#endif
}

int VerifyRsaSignature(const char* algorithm, const uint8_t* der, size_t der_length,
                       const uint8_t* message, size_t message_length,
                       const uint8_t* signature, size_t signature_length) {
    Bytes pkcs1;
    if (der_length > static_cast<size_t>(std::numeric_limits<long>::max()) || !RsaPkcs1({der, der_length}, &pkcs1)) return -2;
    if ((!message && message_length) || (!signature && signature_length) ||
        message_length > static_cast<size_t>(std::numeric_limits<long>::max()) ||
        signature_length > static_cast<size_t>(std::numeric_limits<long>::max())) return -1;
#if defined(__APPLE__)
    Cf<SecKeyRef> key(AppleKey(pkcs1));
    if (!key.value) return -2;
    SecKeyAlgorithm algo = Algorithm(algorithm);
    if (!algo || !SecKeyIsAlgorithmSupported(key.value, kSecKeyOperationTypeVerify, algo)) return -1;
    Cf<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, message, message_length));
    Cf<CFDataRef> sig(CFDataCreate(kCFAllocatorDefault, signature, signature_length));
    if (!data.value || !sig.value) return -1;
    Cf<CFErrorRef> error;
    if (SecKeyVerifySignature(key.value, algo, data.value, sig.value, &error.value)) return 1;
    return error.value && CFErrorGetCode(error.value) != errSecVerifyFailed ? -1 : 0;
#elif defined(KUDROID_HAS_OPENSSL)
    ErrorScope errors;
    Key key = OpenSslKey({der, der_length});
    if (!key) return -2;
    const EVP_MD* digest = Algorithm(algorithm);
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    EVP_PKEY_CTX* key_ctx = nullptr;
    if (!digest || !ctx || EVP_DigestVerifyInit(ctx.get(), &key_ctx, digest, nullptr, key.get()) != 1 ||
        EVP_PKEY_CTX_set_rsa_padding(key_ctx, RSA_PKCS1_PADDING) <= 0 ||
        EVP_DigestVerifyUpdate(ctx.get(), message, message_length) != 1) return -1;
    return EVP_DigestVerifyFinal(ctx.get(), signature, signature_length) == 1 ? 1 : 0;
#else
    return -1;
#endif
}
}  // namespace kudroid
