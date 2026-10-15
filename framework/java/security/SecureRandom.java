package java.security;

import java.util.Random;

public class SecureRandom extends Random {
    public SecureRandom() { super(); }
    public SecureRandom(byte[] seed) { super(); }
    public static SecureRandom getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new SecureRandom();
    }
    public synchronized void setSeed(byte[] seed) {}
    public byte[] generateSeed(int numBytes) {
        byte[] bytes = new byte[numBytes];
        nextBytes(bytes);
        return bytes;
    }
}
