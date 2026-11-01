#include "kudroid/kuart/LibCore.h"
#include "kudroid/kuart/DexClass.h"
#include "kudroid/kuart/DexObject.h"
#include "kudroid/kuart/DexString.h"
#include "kudroid/kuart/DexClassLinker.h"
#include "kudroid/kuart/Interpreter.h"
#include "kudroid/platform/RsaSignature.h"
#include "kudroid/platform/SymmetricCrypto.h"

#include <cstring>
#include <new>
#include <vector>

namespace kudroid::kuart {
namespace {
struct CryptoBytes : std::vector<uint8_t> {
    using std::vector<uint8_t>::vector;
    ~CryptoBytes() {
        volatile uint8_t* p = data();
        for (size_t i = 0; i < size(); ++i) p[i] = 0;
    }
};

DexArray* Bytes(const DexValue& v) {
    auto* array = reinterpret_cast<DexArray*>(v.l);
    return array && array->clazz && array->clazz->descriptor &&
           !std::strcmp(array->clazz->descriptor, "[B") ? array : nullptr;
}

bool ReturnBytes(Interpreter* interp, const std::vector<uint8_t>& bytes, DexValue* result) {
    auto* klass = interp->linker()->FindClass("[B");
    auto* array = klass ? interp->linker()->AllocArray(klass, static_cast<int32_t>(bytes.size())) : nullptr;
    if (!array) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "Crypto result allocation failed");
        return true;
    }
    if (!bytes.empty()) std::memcpy(array->Data(), bytes.data(), bytes.size());
    result->l = array;
    return true;
}

bool InvokeSymmetric(Interpreter* interp, const char* name, const DexValue* args,
                     size_t num_args, DexValue* result) {
    if (!std::strcmp(name, "symmetricAvailable") && num_args == 0) {
        *result = DexValue::Int(SymmetricCryptoAvailable());
        return true;
    }
    if (!std::strcmp(name, "pkcs12Sha1Key") && num_args == 3) {
        auto* password = reinterpret_cast<DexArray*>(args[0].l);
        DexArray* salt = Bytes(args[1]);
        CryptoBytes bytes;
        if (!password || !password->clazz || !password->clazz->descriptor ||
            std::strcmp(password->clazz->descriptor, "[C") || !salt ||
            !Pkcs12Sha1Key(reinterpret_cast<const uint16_t*>(password->Data()), password->length,
                          salt->Data(), salt->length, args[2].i, &bytes)) {
            interp->ThrowException("Ljava/security/spec/InvalidKeySpecException;", "PBE derivation failed");
            return true;
        }
        return ReturnBytes(interp, bytes, result);
    }
    if (!std::strcmp(name, "aesCbc") && num_args == 4) {
        DexArray* key = Bytes(args[1]);
        DexArray* iv = Bytes(args[2]);
        DexArray* input = Bytes(args[3]);
        CryptoBytes bytes;
        int status = key && iv && input ? AesCbc(args[0].i != 0, key->Data(), key->length,
            iv->Data(), iv->length, input->Data(), input->length, &bytes) : -1;
        if (status) {
            const char* exception = status == -2 ? "Ljavax/crypto/IllegalBlockSizeException;" :
                status == -3 ? "Ljavax/crypto/BadPaddingException;" : "Ljava/security/ProviderException;";
            interp->ThrowException(exception, "AES CBC operation failed");
            return true;
        }
        return ReturnBytes(interp, bytes, result);
    }
    if (!std::strcmp(name, "randomBytes") && num_args == 1) {
        if (args[0].i < 0) {
            interp->ThrowException("Ljava/lang/IllegalArgumentException;", "Negative random byte count");
            return true;
        }
        CryptoBytes bytes(static_cast<size_t>(args[0].i));
        if (!CryptoRandomBytes(bytes.data(), bytes.size())) {
            interp->ThrowException("Ljava/security/ProviderException;", "Random source unavailable");
            return true;
        }
        return ReturnBytes(interp, bytes, result);
    }
    return false;
}
}

bool Invoke_libcore_io_KuCrypto(Interpreter* interp, const char* name, const DexValue* args,
                               size_t num_args, DexValue* result) {
    if (!std::strcmp(name, "rsaAvailable") && num_args == 0) {
        *result = DexValue::Int(RsaAvailable());
        return true;
    }
    if (!std::strcmp(name, "validateRsaSpki") && num_args == 1) {
        DexArray* key = Bytes(args[0]);
        *result = DexValue::Int(key && ValidateRsaSpki(key->Data(), key->length));
        return true;
    }
    if (!std::strcmp(name, "verifyRsaSignature") && num_args == 4) {
        auto* algorithm = reinterpret_cast<DexString*>(args[0].l);
        DexArray* key = Bytes(args[1]);
        DexArray* message = Bytes(args[2]);
        DexArray* signature = Bytes(args[3]);
        *result = DexValue::Int(algorithm && key && message && signature
            ? VerifyRsaSignature(algorithm->utf8, key->Data(), key->length,
                                 message->Data(), message->length, signature->Data(), signature->length) : -1);
        return true;
    }
    try {
        return InvokeSymmetric(interp, name, args, num_args, result);
    } catch (const std::bad_alloc&) {
        interp->ThrowException("Ljava/lang/OutOfMemoryError;", "Crypto allocation failed");
        return true;
    }
}
}  // namespace kudroid::kuart
