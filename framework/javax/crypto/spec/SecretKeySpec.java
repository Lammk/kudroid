package javax.crypto.spec;

import java.security.spec.KeySpec;
import java.util.Arrays;
import javax.crypto.SecretKey;

public class SecretKeySpec implements SecretKey, KeySpec {
    private final byte[] key;
    private final String algorithm;

    public SecretKeySpec(byte[] key, String algorithm) {
        this(key, 0, key == null ? 0 : key.length, algorithm);
    }
    public SecretKeySpec(byte[] key, int offset, int length, String algorithm) {
        if (key == null || algorithm == null || key.length == 0)
            throw new IllegalArgumentException("Missing key or algorithm");
        if (offset > key.length || length > (long)key.length - offset)
            throw new IllegalArgumentException("Invalid key range");
        if (offset < 0 || length < 0) throw new ArrayIndexOutOfBoundsException();
        this.key = new byte[length];
        System.arraycopy(key, offset, this.key, 0, length);
        this.algorithm = algorithm;
    }
    public String getAlgorithm() { return algorithm; }
    public String getFormat() { return "RAW"; }
    public byte[] getEncoded() { return Arrays.copyOf(key, key.length); }
    public boolean equals(Object other) {
        if (!(other instanceof SecretKey)) return false;
        SecretKey secret = (SecretKey) other;
        String name = secret.getAlgorithm();
        boolean same = algorithm.equalsIgnoreCase(name) ||
            ((algorithm.equalsIgnoreCase("DESede") || algorithm.equalsIgnoreCase("TripleDES")) &&
             ("DESede".equalsIgnoreCase(name) || "TripleDES".equalsIgnoreCase(name)));
        return same && Arrays.equals(key, secret.getEncoded());
    }
    public int hashCode() {
        int hash = 0;
        for (int i = 1; i < key.length; i++) hash += key[i] * i;
        String name = algorithm.equalsIgnoreCase("TripleDES") ? "desede" :
            algorithm.toLowerCase(java.util.Locale.ENGLISH);
        return hash ^ name.hashCode();
    }
}
