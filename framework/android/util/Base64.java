package android.util;

/** Base64 as the platform defines it: the flags select padding, line wrapping and alphabet. */
public class Base64 {
    public static final int DEFAULT = 0;
    public static final int NO_PADDING = 1;
    public static final int NO_WRAP = 2;
    public static final int CRLF = 4;
    public static final int URL_SAFE = 8;
    public static final int NO_CLOSE = 16;

    private static final String STANDARD = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    private static final String URL = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    private static final int LINE = 76;

    private Base64() {}

    // ── decoding ─────────────────────────────────────────────────────────────

    public static byte[] decode(String str, int flags) {
        return decode(str.getBytes(), flags);
    }

    public static byte[] decode(byte[] input, int flags) {
        return decode(input, 0, input.length, flags);
    }

    /**
     * Whitespace is skipped, '=' ends the data, and either alphabet is accepted for the 62nd and
     * 63rd characters. Any other character is an error, as on the platform.
     */
    public static byte[] decode(byte[] input, int offset, int len, int flags) {
        byte[] out = new byte[len * 3 / 4 + 3];
        int n = 0;
        int acc = 0;
        int bits = 0;
        for (int i = offset; i < offset + len; i++) {
            int c = input[i] & 0xff;
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+' || c == '-') v = 62;
            else if (c == '/' || c == '_') v = 63;
            else if (c == '=') break;
            else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
            else throw new IllegalArgumentException("bad base-64");
            acc = (acc << 6) | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out[n++] = (byte) ((acc >> bits) & 0xff);
            }
        }
        byte[] result = new byte[n];
        System.arraycopy(out, 0, result, 0, n);
        return result;
    }

    // ── encoding ─────────────────────────────────────────────────────────────

    public static String encodeToString(byte[] input, int flags) {
        return new String(encode(input, flags));
    }

    public static String encodeToString(byte[] input, int offset, int len, int flags) {
        return new String(encode(input, offset, len, flags));
    }

    public static byte[] encode(byte[] input, int flags) {
        return encode(input, 0, input.length, flags);
    }

    public static byte[] encode(byte[] input, int offset, int len, int flags) {
        String alphabet = (flags & URL_SAFE) != 0 ? URL : STANDARD;
        boolean pad = (flags & NO_PADDING) == 0;
        boolean wrap = (flags & NO_WRAP) == 0;
        String eol = (flags & CRLF) != 0 ? "\r\n" : "\n";
        StringBuilder out = new StringBuilder(len * 4 / 3 + 8);
        int column = 0;
        for (int i = 0; i < len; i += 3) {
            int b0 = input[offset + i] & 0xff;
            int b1 = i + 1 < len ? input[offset + i + 1] & 0xff : 0;
            int b2 = i + 2 < len ? input[offset + i + 2] & 0xff : 0;
            int group = (b0 << 16) | (b1 << 8) | b2;
            int chars = i + 2 < len ? 4 : (i + 1 < len ? 3 : 2);
            for (int k = 0; k < 4; k++) {
                if (wrap && column == LINE) {
                    out.append(eol);
                    column = 0;
                }
                if (k < chars) {
                    out.append(alphabet.charAt((group >> (18 - 6 * k)) & 0x3f));
                    column++;
                } else if (pad) {
                    out.append('=');
                    column++;
                }
            }
        }
        if (wrap && len > 0) out.append(eol);
        return out.toString().getBytes();
    }
}
