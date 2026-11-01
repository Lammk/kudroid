#include "kudroid/platform/SymmetricCrypto.h"

#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
void Check(bool value, const char* label) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", label); }
}
std::vector<uint8_t> Hex(const char* text) {
    std::vector<uint8_t> bytes;
    for (size_t n = 0; text[n]; n += 2) {
        unsigned int byte = 0;
        std::sscanf(text + n, "%2x", &byte);
        bytes.push_back(static_cast<uint8_t>(byte));
    }
    return bytes;
}
}

int main() {
    Check(kudroid::SymmetricCryptoAvailable(), "host crypto backend present");
    const std::vector<std::vector<uint16_t>> passwords = {
        {}, {'p', 'a', 's', 's'}, {'a', 0, 'b'}, {0x03a3, 0x00e9, 0x4e2d, 0xd83d, 0xde00},
        std::vector<uint16_t>(79, 0x00e9)};
    for (const auto& password : passwords) {
        for (size_t length : {size_t(1), size_t(8), size_t(63), size_t(64), size_t(65), size_t(128)}) {
            std::vector<uint8_t> salt(length);
            for (size_t n = 0; n < salt.size(); ++n) salt[n] = static_cast<uint8_t>(n * 17 + 3);
            std::vector<uint8_t> encoded(password.empty() ? 0 : (password.size() + 1) * 2);
            for (size_t n = 0; n < password.size(); ++n) {
                encoded[n * 2] = static_cast<uint8_t>(password[n] >> 8);
                encoded[n * 2 + 1] = static_cast<uint8_t>(password[n]);
            }
            std::array<uint8_t, 32> reference{};
            std::vector<uint8_t> actual;
            const bool expected = PKCS12_key_gen_uni(encoded.data(), static_cast<int>(encoded.size()),
                salt.data(), static_cast<int>(salt.size()), PKCS12_KEY_ID, 17,
                reference.size(), reference.data(), EVP_sha1()) == 1;
            Check(expected && kudroid::Pkcs12Sha1Key(password.data(), password.size(), salt.data(), salt.size(), 17, &actual)
                && actual.size() == reference.size() && std::equal(actual.begin(), actual.end(), reference.begin()),
                "PKCS12 matches independent OpenSSL derivation");
        }
    }
    const auto iv = Hex("000102030405060708090a0b0c0d0e0f");
    const auto plaintext = Hex("6bc1bee22e409f96e93d7e117393172a");
    const char* key_text[] = {"2b7e151628aed2a6abf7158809cf4f3c",
        "8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b",
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"};
    const char* ciphertext_text[] = {"7649abac8119b246cee98e9b12e9197d",
        "4f021db243bc633d7178183a9fa071e8", "f58c4c04d6e5f1ba779eabfb5f7bfbd6"};
    for (size_t n = 0; n < 3; ++n) {
        const auto key = Hex(key_text[n]);
        const auto expected = Hex(ciphertext_text[n]);
        std::vector<uint8_t> encrypted, decrypted;
        Check(kudroid::AesCbc(true, key.data(), key.size(), iv.data(), iv.size(), plaintext.data(), plaintext.size(), &encrypted) == 0
            && encrypted.size() == 32 && std::equal(expected.begin(), expected.end(), encrypted.begin()), "NIST AES CBC first block");
        Check(kudroid::AesCbc(false, key.data(), key.size(), iv.data(), iv.size(), encrypted.data(), encrypted.size(), &decrypted) == 0
            && decrypted == plaintext, "NIST AES CBC round trip with padding");
        Check(kudroid::AesCbc(false, key.data(), key.size(), iv.data(), iv.size(), encrypted.data(), 15, &decrypted) == -2
            && decrypted.empty(), "invalid ciphertext length cannot expose plaintext");
        // Corrupt the previous block to force an invalid final padding byte.
        encrypted[15] ^= 0x10;
        Check(kudroid::AesCbc(false, key.data(), key.size(), iv.data(), iv.size(), encrypted.data(), encrypted.size(), &decrypted) == -3
            && decrypted.empty(), "invalid padding cannot expose partial plaintext");
        Check(kudroid::AesCbc(true, key.data(), key.size(), iv.data(), iv.size(), nullptr, 0, &encrypted) == 0
            && encrypted.size() == 16, "empty input produces padding");
    }
    std::vector<uint8_t> output;
    const uint8_t byte = 1;
    Check(!kudroid::Pkcs12Sha1Key(nullptr, 0, &byte, 1, 0, &output), "invalid iterations rejected");
    Check(!kudroid::Pkcs12Sha1Key(nullptr, 0, nullptr, 0, 1, &output), "missing salt rejected");
    Check(!kudroid::Pkcs12Sha1Key(reinterpret_cast<const uint16_t*>(&byte), std::numeric_limits<size_t>::max(),
        &byte, 1, 1, &output), "password length overflow rejected before access");
    Check(kudroid::AesCbc(true, iv.data(), iv.size(), iv.data(), iv.size(), &byte,
        std::numeric_limits<size_t>::max(), &output) == -2, "cipher length overflow rejected before access");
    Check(kudroid::AesCbc(true, iv.data(), 15, iv.data(), iv.size(), &byte, 1, &output) == -1, "invalid key length rejected");
    std::array<uint8_t, 16> random{};
    Check(kudroid::CryptoRandomBytes(random.data(), random.size()), "native random source succeeds");
    std::printf("Symmetric crypto: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
