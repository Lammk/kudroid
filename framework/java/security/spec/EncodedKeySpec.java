package java.security.spec;

public abstract class EncodedKeySpec implements KeySpec {
    private final byte[] encoded;

    public EncodedKeySpec(byte[] encodedKey) {
        if (encodedKey == null) throw new NullPointerException();
        encoded = java.util.Arrays.copyOf(encodedKey, encodedKey.length);
    }

    public byte[] getEncoded() { return java.util.Arrays.copyOf(encoded, encoded.length); }
    public abstract String getFormat();
}
