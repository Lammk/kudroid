#include "kudroid/platform/SymmetricCrypto.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

#if defined(__APPLE__)
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonDigest.h>
#include <Security/SecRandom.h>
#elif defined(KUDROID_HAS_OPENSSL)
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace kudroid {
namespace {
void Wipe(uint8_t* bytes, size_t size) {
    volatile uint8_t* p = bytes;
    while (size--) *p++ = 0;
}
struct SensitiveBytes : std::vector<uint8_t> {
    using std::vector<uint8_t>::vector;
    ~SensitiveBytes() { Wipe(data(), size()); }
};
struct DigestBytes : std::array<uint8_t, 20> {
    ~DigestBytes() { Wipe(data(), size()); }
};

#if defined(KUDROID_HAS_OPENSSL) && !defined(__APPLE__)
struct ErrorScope {
    bool marked = ERR_set_mark() != 0;
    ~ErrorScope() { if (marked) ERR_pop_to_mark(); else ERR_clear_error(); }
};
#endif

bool Sha1(const uint8_t* input, size_t size, uint8_t* output) {
#if defined(__APPLE__)
    CC_SHA1_CTX context;
    CC_SHA1_Init(&context);
    while (size) {
        size_t chunk = std::min(size, static_cast<size_t>(std::numeric_limits<CC_LONG>::max()));
        CC_SHA1_Update(&context, input, static_cast<CC_LONG>(chunk));
        input += chunk;
        size -= chunk;
    }
    CC_SHA1_Final(output, &context);
    Wipe(reinterpret_cast<uint8_t*>(&context), sizeof(context));
    return true;
#elif defined(KUDROID_HAS_OPENSSL)
    ErrorScope errors;
    unsigned int length = 0;
    return EVP_Digest(input, size, output, &length, EVP_sha1(), nullptr) == 1 && length == 20;
#else
    return false;
#endif
}
}  // namespace

bool SymmetricCryptoAvailable() {
#if defined(__APPLE__) || defined(KUDROID_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}

bool Pkcs12Sha1Key(const uint16_t* password, size_t password_length,
                  const uint8_t* salt, size_t salt_length, int iterations,
                  std::vector<uint8_t>* output) {
    if (!output) return false;
    output->clear();
    if (iterations <= 0 || !salt || !salt_length || (password_length && !password) ||
        password_length > (std::numeric_limits<size_t>::max() - 2) / 2) return false;
    // PKCS12 uses big-endian UTF-16 plus a terminator for nonempty passwords.
    const size_t password_bytes = password_length ? (password_length + 1) * 2 : 0;
    if (salt_length > std::numeric_limits<size_t>::max() - 63 ||
        password_bytes > std::numeric_limits<size_t>::max() - 63) return false;
    const size_t salt_blocks = ((salt_length + 63) / 64) * 64;
    const size_t password_blocks = ((password_bytes + 63) / 64) * 64;
    if (salt_blocks > std::numeric_limits<size_t>::max() - 64 ||
        password_blocks > std::numeric_limits<size_t>::max() - 64 - salt_blocks) return false;
    SensitiveBytes material(64 + salt_blocks + password_blocks, 1);
    for (size_t i = 0; i < salt_blocks; ++i) material[64 + i] = salt[i % salt_length];
    for (size_t i = 0; i < password_blocks; ++i) {
        const size_t byte = i % password_bytes;
        const uint16_t unit = byte / 2 < password_length ? password[byte / 2] : 0;
        material[64 + salt_blocks + i] = static_cast<uint8_t>(byte % 2 ? unit : unit >> 8);
    }
    SensitiveBytes derived(32);
    DigestBytes digest{};
    for (size_t offset = 0; offset < derived.size(); offset += digest.size()) {
        if (!Sha1(material.data(), material.size(), digest.data())) return false;
        for (int round = 1; round < iterations; ++round) {
            DigestBytes next{};
            if (!Sha1(digest.data(), digest.size(), next.data())) return false;
            std::copy(next.begin(), next.end(), digest.begin());
        }
        std::copy_n(digest.begin(), std::min(digest.size(), derived.size() - offset), derived.begin() + offset);
        // Add the repeated digest and one to each big-endian 512-bit block.
        for (size_t block = 64; block < material.size(); block += 64) {
            unsigned int carry = 1;
            for (size_t j = 64; j-- > 0;) {
                carry += material[block + j] + digest[j % digest.size()];
                material[block + j] = static_cast<uint8_t>(carry);
                carry >>= 8;
            }
        }
    }
    output->assign(derived.begin(), derived.end());
    return true;
}

int AesCbc(bool encrypt, const uint8_t* key, size_t key_length,
           const uint8_t* iv, size_t iv_length, const uint8_t* input,
           size_t input_length, std::vector<uint8_t>* output) {
    if (!output) return -1;
    output->clear();
    if (!key || (key_length != 16 && key_length != 24 && key_length != 32) ||
        !iv || iv_length != 16 || (input_length && !input)) return -1;
    if ((!encrypt && input_length % 16) ||
        input_length > static_cast<size_t>(std::numeric_limits<int>::max() - 16)) return -2;
    if (!encrypt && !input_length) return 0;
    SensitiveBytes bytes(input_length + 16);
#if defined(__APPLE__)
    size_t count = 0;
    const auto status = CCCrypt(encrypt ? kCCEncrypt : kCCDecrypt, kCCAlgorithmAES,
        kCCOptionPKCS7Padding, key, key_length, iv, input, input_length,
        bytes.data(), bytes.size(), &count);
    if (status != kCCSuccess) return status == kCCDecodeError ? -3 : status == kCCAlignmentError ? -2 : -1;
    output->assign(bytes.begin(), bytes.begin() + count);
    return 0;
#elif defined(KUDROID_HAS_OPENSSL)
    ErrorScope errors;
    const EVP_CIPHER* algorithm = key_length == 16 ? EVP_aes_128_cbc() :
        key_length == 24 ? EVP_aes_192_cbc() : EVP_aes_256_cbc();
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    int count = 0, tail = 0;
    if (!context || EVP_CipherInit_ex(context.get(), algorithm, nullptr, key, iv, encrypt) != 1 ||
        EVP_CipherUpdate(context.get(), bytes.data(), &count, input, static_cast<int>(input_length)) != 1) return -1;
    if (EVP_CipherFinal_ex(context.get(), bytes.data() + count, &tail) != 1) return encrypt ? -1 : -3;
    output->assign(bytes.begin(), bytes.begin() + count + tail);
    return 0;
#else
    return -1;
#endif
}

bool CryptoRandomBytes(uint8_t* output, size_t length) {
    if ((length && !output) || length > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    if (!length) return true;
#if defined(__APPLE__)
    return SecRandomCopyBytes(kSecRandomDefault, length, output) == errSecSuccess;
#elif defined(KUDROID_HAS_OPENSSL)
    ErrorScope errors;
    return RAND_bytes(output, static_cast<int>(length)) == 1;
#else
    return false;
#endif
}
}  // namespace kudroid
