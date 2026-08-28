package java.security;

public abstract class MessageDigest {
    private final String algorithm;

    protected MessageDigest(String algorithm) { this.algorithm = algorithm; }
    public static MessageDigest getInstance(String algorithm) throws NoSuchAlgorithmException {
        return new MessageDigest(algorithm) {
            public byte[] digest() { return new byte[32]; }
            public void update(byte input) {}
            public void update(byte[] input, int offset, int len) {}
            public void reset() {}
        };
    }
    public void update(byte input) {}
    public void update(byte[] input, int offset, int len) {}
    public void update(byte[] input) { update(input, 0, input.length); }
    public byte[] digest() { return new byte[32]; }
    public byte[] digest(byte[] input) { update(input); return digest(); }
    public void reset() {}
    public final String getAlgorithm() { return algorithm; }
}
