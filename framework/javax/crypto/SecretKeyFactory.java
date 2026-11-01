package javax.crypto;

import java.security.NoSuchAlgorithmException;
import java.security.spec.InvalidKeySpecException;
import java.security.spec.KeySpec;
import javax.crypto.spec.PBEKeySpec;
import javax.crypto.spec.SecretKeySpec;
import libcore.io.KuCrypto;

public class SecretKeyFactory {
    private final String algorithm;
    private SecretKeyFactory(String algorithm) { this.algorithm = algorithm; }

    public static SecretKeyFactory getInstance(String algorithm) throws NoSuchAlgorithmException {
        if (algorithm == null) throw new NullPointerException("algorithm");
        if (!"PBEWITHSHAAND256BITAES-CBC-BC".equalsIgnoreCase(algorithm) || !KuCrypto.symmetricAvailable())
            throw new NoSuchAlgorithmException(algorithm);
        return new SecretKeyFactory(algorithm);
    }
    public final String getAlgorithm() { return algorithm; }

    public final SecretKey generateSecret(KeySpec keySpec) throws InvalidKeySpecException {
        if (!(keySpec instanceof PBEKeySpec)) throw new InvalidKeySpecException("Expected PBEKeySpec");
        PBEKeySpec spec = (PBEKeySpec) keySpec;
        byte[] salt = spec.getSalt();
        if (salt == null) throw new InvalidKeySpecException("Salt and iterations required");
        char[] password = spec.getPassword();
        byte[] encoded = null;
        try {
            encoded = KuCrypto.pkcs12Sha1Key(password, salt, spec.getIterationCount());
            return new SecretKeySpec(encoded, algorithm);
        } finally {
            for (int i = 0; i < password.length; i++) password[i] = 0;
            if (encoded != null) java.util.Arrays.fill(encoded, (byte) 0);
        }
    }
}
