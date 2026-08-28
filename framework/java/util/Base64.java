package java.util;

public class Base64 {
    private Base64() {}

    public static class Encoder {
        public byte[] encode(byte[] src) {
            return android.util.Base64.encode(src, android.util.Base64.NO_WRAP);
        }
        public String encodeToString(byte[] src) {
            return android.util.Base64.encodeToString(src, android.util.Base64.NO_WRAP);
        }
    }

    public static class Decoder {
        public byte[] decode(byte[] src) {
            return android.util.Base64.decode(src, android.util.Base64.DEFAULT);
        }
        public byte[] decode(String src) {
            return android.util.Base64.decode(src, android.util.Base64.DEFAULT);
        }
    }

    public static Encoder getEncoder() { return new Encoder(); }
    public static Encoder getUrlEncoder() { return new Encoder(); }
    public static Encoder getMimeEncoder() { return new Encoder(); }
    public static Decoder getDecoder() { return new Decoder(); }
    public static Decoder getUrlDecoder() { return new Decoder(); }
    public static Decoder getMimeDecoder() { return new Decoder(); }
}
