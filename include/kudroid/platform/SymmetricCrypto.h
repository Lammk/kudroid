#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kudroid {

bool SymmetricCryptoAvailable();
bool Pkcs12Sha1Key(const uint16_t* password, size_t password_length,
                  const uint8_t* salt, size_t salt_length, int iterations,
                  std::vector<uint8_t>* output);
// 0 success, -1 backend error, -2 invalid block size, -3 invalid padding.
int AesCbc(bool encrypt, const uint8_t* key, size_t key_length,
           const uint8_t* iv, size_t iv_length, const uint8_t* input,
           size_t input_length, std::vector<uint8_t>* output);
bool CryptoRandomBytes(uint8_t* output, size_t length);

}  // namespace kudroid
