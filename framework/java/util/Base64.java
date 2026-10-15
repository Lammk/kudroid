package java.util;

public class Base64 {
    private Base64() {}

    public static class Encoder {
        private final int flags;

        Encoder(int flags) { this.flags = flags; }

        public Encoder withoutPadding() {
            return new Encoder(flags | android.util.Base64.NO_PADDING);
        }

        public byte[] encode(byte[] src) {
            return android.util.Base64.encode(src, flags);
        }

        public String encodeToString(byte[] src) {
            return android.util.Base64.encodeToString(src, flags);
        }
    }

    public static class Decoder {
        private final int flags;

        Decoder(int flags) { this.flags = flags; }

        public byte[] decode(byte[] src) {
            return android.util.Base64.decode(src, flags);
        }

        public byte[] decode(String src) {
            return android.util.Base64.decode(src, flags);
        }
    }

    public static Encoder getEncoder() { return new Encoder(android.util.Base64.NO_WRAP); }
    public static Encoder getUrlEncoder() {
        return new Encoder(android.util.Base64.NO_WRAP | android.util.Base64.URL_SAFE);
    }
    public static Encoder getMimeEncoder() { return new Encoder(android.util.Base64.CRLF); }
    public static Decoder getDecoder() { return new Decoder(android.util.Base64.DEFAULT); }
    public static Decoder getUrlDecoder() { return new Decoder(android.util.Base64.URL_SAFE); }
    public static Decoder getMimeDecoder() { return new Decoder(android.util.Base64.DEFAULT); }
}
