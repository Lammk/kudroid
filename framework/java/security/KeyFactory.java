package java.security;

import java.security.spec.InvalidKeySpecException;
import java.security.spec.KeySpec;
import java.security.spec.X509EncodedKeySpec;
import libcore.io.KuCrypto;

public class KeyFactory {
    private final String algorithm;
    private KeyFactory(String algorithm) { this.algorithm = algorithm; }

    public static KeyFactory getInstance(String algorithm) throws NoSuchAlgorithmException {
        if (algorithm == null) throw new NullPointerException();
        if (!"RSA".equalsIgnoreCase(algorithm) || !KuCrypto.rsaAvailable())
            throw new NoSuchAlgorithmException(algorithm);
        return new KeyFactory(algorithm);
    }

    public final String getAlgorithm() { return algorithm; }

    public final PublicKey generatePublic(KeySpec spec) throws InvalidKeySpecException {
        if (!(spec instanceof X509EncodedKeySpec))
            throw new InvalidKeySpecException("Expected an X.509 public key specification");
        byte[] encoded = ((X509EncodedKeySpec) spec).getEncoded();
        if (!KuCrypto.validateRsaSpki(encoded))
            throw new InvalidKeySpecException("Invalid RSA X.509 public key");
        return new RsaPublicKey(encoded);
    }

    private static final class RsaPublicKey implements PublicKey {
        private final byte[] encoded;
        RsaPublicKey(byte[] encoded) { this.encoded = java.util.Arrays.copyOf(encoded, encoded.length); }
        public String getAlgorithm() { return "RSA"; }
        public String getFormat() { return "X.509"; }
        public byte[] getEncoded() { return java.util.Arrays.copyOf(encoded, encoded.length); }
    }
}
