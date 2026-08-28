package java.security;

import java.util.Random;

public class SecureRandom extends Random {
    private static final long serialVersionUID = 4940670005562187L;

    public SecureRandom() { super(); }
    public SecureRandom(byte[] seed) { super(); setSeed(seed); }

    public static SecureRandom getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new SecureRandom();
    }
    public static SecureRandom getInstance(String algorithm, String provider) throws NoSuchAlgorithmException, NoSuchProviderException {
        return new SecureRandom();
    }
    public static SecureRandom getInstance(String algorithm, Provider provider) throws NoSuchAlgorithmException {
        return new SecureRandom();
    }

    public synchronized void setSeed(byte[] seed) {}
    public void nextBytes(byte[] bytes) {
        for (int i = 0; i < bytes.length; i++) {
            bytes[i] = (byte) nextInt(256);
        }
    }
    public static byte[] getSeed(int numBytes) {
        byte[] bytes = new byte[numBytes];
        new SecureRandom().nextBytes(bytes);
        return bytes;
    }
    public byte[] generateSeed(int numBytes) {
        return getSeed(numBytes);
    }
}
