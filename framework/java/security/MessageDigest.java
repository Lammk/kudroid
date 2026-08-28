package java.security;

public abstract class MessageDigest {
    private final String algorithm;

    protected MessageDigest(String algorithm) {
        this.algorithm = algorithm;
    }
    public static MessageDigest getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new MessageDigest(algorithm) {
            private byte[] buf = new byte[0];
            protected void engineUpdate(byte input) {}
            protected void engineUpdate(byte[] input, int offset, int len) {}
            protected byte[] engineDigest() { return new byte[32]; }
            protected void engineReset() {}
        };
    }
    public static MessageDigest getInstance(String algorithm, String provider) throws NoSuchAlgorithmException, NoSuchProviderException {
        return getInstance(algorithm);
    }
    public static MessageDigest getInstance(String algorithm, Provider provider) throws NoSuchAlgorithmException {
        return getInstance(algorithm);
    }

    public void update(byte input) { engineUpdate(input); }
    public void update(byte[] input, int offset, int len) { engineUpdate(input, offset, len); }
    public void update(byte[] input) { engineUpdate(input, 0, input.length); }
    public byte[] digest() { return engineDigest(); }
    public int digest(byte[] buf, int offset, int len) throws DigestException {
        byte[] digest = engineDigest();
        if (len < digest.length) throw new DigestException("insufficient space in the output buffer to store the resulting digest");
        if (buf.length - offset < digest.length) throw new DigestException("insufficient space in the output buffer to store the resulting digest");
        System.arraycopy(digest, 0, buf, offset, digest.length);
        return digest.length;
    }
    public byte[] digest(byte[] input) {
        update(input);
        return digest();
    }
    public void reset() { engineReset(); }
    public final String getAlgorithm() { return algorithm; }

    public static boolean isEqual(byte[] digesta, byte[] digestb) {
        if (digesta == digestb) return true;
        if (digesta == null || digestb == null) return false;
        if (digesta.length != digestb.length) return false;
        int result = 0;
        for (int i = 0; i < digesta.length; i++) {
            result |= digesta[i] ^ digestb[i];
        }
        return result == 0;
    }

    protected abstract void engineUpdate(byte input);
    protected abstract void engineUpdate(byte[] input, int offset, int len);
    protected abstract byte[] engineDigest();
    protected abstract void engineReset();
}
