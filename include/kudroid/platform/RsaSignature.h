#pragma once

#include <cstddef>
#include <cstdint>

namespace kudroid {

bool RsaAvailable();
bool ValidateRsaSpki(const uint8_t* der, size_t length);
// 1 verified, 0 mismatch, -1 backend/algorithm error, -2 invalid key.
int VerifyRsaSignature(const char* algorithm, const uint8_t* der, size_t der_length,
                       const uint8_t* message, size_t message_length,
                       const uint8_t* signature, size_t signature_length);

}  // namespace kudroid
