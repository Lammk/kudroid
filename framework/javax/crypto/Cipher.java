package javax.crypto;

import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.Key;
import java.security.NoSuchAlgorithmException;
import java.security.spec.AlgorithmParameterSpec;
import java.util.Arrays;
import javax.crypto.spec.IvParameterSpec;
import libcore.io.KuCrypto;

public class Cipher {
    public static final int ENCRYPT_MODE = 1;
    public static final int DECRYPT_MODE = 2;
    public static final int WRAP_MODE = 3;
    public static final int UNWRAP_MODE = 4;
    public static final int PUBLIC_KEY = 1;
    public static final int PRIVATE_KEY = 2;
    public static final int SECRET_KEY = 3;
    private final String algorithm;
    private byte[] key;
    private byte[] iv;
    private boolean encrypt;

    private Cipher(String algorithm) { this.algorithm = algorithm; }

    public static Cipher getInstance(String transformation)
            throws NoSuchAlgorithmException, NoSuchPaddingException {
        if (transformation == null) throw new NoSuchAlgorithmException("Null transformation");
        String[] parts = transformation.split("/", -1);
        if (parts.length != 3 || !"AES".equalsIgnoreCase(parts[0].trim()) ||
                !"CBC".equalsIgnoreCase(parts[1].trim()) || !KuCrypto.symmetricAvailable())
            throw new NoSuchAlgorithmException(transformation);
        if (!"PKCS5Padding".equalsIgnoreCase(parts[2].trim()))
            throw new NoSuchPaddingException(parts[2]);
        return new Cipher(transformation);
    }

    public final String getAlgorithm() { return algorithm; }
    public final int getBlockSize() { return 16; }
    public final byte[] getIV() { return iv == null ? null : Arrays.copyOf(iv, iv.length); }
    public final int getOutputSize(int inputLength) {
        if (key == null) throw new IllegalStateException("Cipher not initialized");
        if (inputLength < 0) throw new IllegalArgumentException("Negative input length");
        long size = encrypt ? ((long)inputLength / 16 + 1) * 16 : ((long)inputLength + 15) / 16 * 16;
        if (size > Integer.MAX_VALUE) throw new IllegalArgumentException("Input too large");
        return (int)size;
    }

    public final void init(int opmode, Key key) throws InvalidKeyException {
        try { init(opmode, key, (AlgorithmParameterSpec) null); }
        catch (InvalidAlgorithmParameterException error) { throw new InvalidKeyException(error.getMessage()); }
    }

    public final void init(int opmode, Key newKey, AlgorithmParameterSpec parameters)
            throws InvalidKeyException, InvalidAlgorithmParameterException {
        if (key != null) Arrays.fill(key, (byte) 0);
        key = iv = null;
        if (opmode < ENCRYPT_MODE || opmode > UNWRAP_MODE) throw new IllegalArgumentException("Invalid opmode");
        if (opmode != ENCRYPT_MODE && opmode != DECRYPT_MODE)
            throw new InvalidKeyException("Key wrapping is unavailable");
        if (!(newKey instanceof SecretKey) || !"AES".equalsIgnoreCase(newKey.getAlgorithm()) ||
                !"RAW".equalsIgnoreCase(newKey.getFormat())) throw new InvalidKeyException("Expected raw AES key");
        byte[] encoded = newKey.getEncoded();
        if (encoded == null || (encoded.length != 16 && encoded.length != 24 && encoded.length != 32))
            throw new InvalidKeyException("AES key must contain 16, 24 or 32 bytes");
        byte[] nextIV;
        if (parameters == null) {
            if (opmode == DECRYPT_MODE) throw new InvalidAlgorithmParameterException("IV required for decryption");
            nextIV = KuCrypto.randomBytes(16);
        } else {
            if (!(parameters instanceof IvParameterSpec))
                throw new InvalidAlgorithmParameterException("Expected IvParameterSpec");
            nextIV = ((IvParameterSpec) parameters).getIV();
            if (nextIV.length != 16) throw new InvalidAlgorithmParameterException("AES IV must contain 16 bytes");
        }
        key = Arrays.copyOf(encoded, encoded.length);
        iv = nextIV;
        encrypt = opmode == ENCRYPT_MODE;
    }

    public final byte[] doFinal() throws IllegalBlockSizeException, BadPaddingException {
        return doFinal(new byte[0]);
    }
    public final byte[] doFinal(byte[] input) throws IllegalBlockSizeException, BadPaddingException {
        return doFinal(input, 0, input == null ? 0 : input.length);
    }
    public final byte[] doFinal(byte[] input, int offset, int length)
            throws IllegalBlockSizeException, BadPaddingException {
        if (key == null) throw new IllegalStateException("Cipher not initialized");
        if (input == null || offset < 0 || length < 0 || offset > input.length - length)
            throw new IllegalArgumentException("Invalid input range");
        byte[] bytes = new byte[length];
        System.arraycopy(input, offset, bytes, 0, length);
        try { return KuCrypto.aesCbc(encrypt, key, iv, bytes); }
        finally { Arrays.fill(bytes, (byte) 0); }
    }
}
