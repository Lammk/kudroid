package android.util;

public class Base64 {
    public static final int DEFAULT = 0;
    public static final int NO_PADDING = 1;
    public static final int NO_WRAP = 2;
    public static final int CRLF = 4;
    public static final int URL_SAFE = 8;
    public static final int NO_CLOSE = 16;

    private static final String CODES = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

    public static byte[] decode(String str, int flags) {
        if (str == null) return new byte[0];
        try {
            return decode(str.getBytes("US-ASCII"), flags);
        } catch (Exception e) {
            return new byte[0];
        }
    }
    public static byte[] decode(byte[] input, int flags) {
        return decode(input, 0, input != null ? input.length : 0, flags);
    }
    public static byte[] decode(byte[] input, int offset, int len, int flags) {
        if (input == null || len <= 0) return new byte[0];
        int pad = 0;
        for (int i = len - 1; i >= 0 && input[offset + i] == '='; i--) pad++;
        int length = (len * 6) / 8 - pad;
        byte[] raw = new byte[length];
        int rawIndex = 0;
        for (int i = 0; i < len; i += 4) {
            int block = (CODES.indexOf((char)input[offset + i]) << 18)
                      + (CODES.indexOf((char)input[offset + i + 1]) << 12)
                      + (CODES.indexOf((char)input[offset + i + 2]) << 6)
                      + (CODES.indexOf((char)input[offset + i + 3]));
            for (int r = 0; r < 3 && rawIndex + r < length; r++) {
                raw[rawIndex + r] = (byte)((block >> (8 * (2 - r))) & 0xFF);
            }
            rawIndex += 3;
        }
        return raw;
    }
    public static String encodeToString(byte[] input, int flags) {
        return new String(encode(input, flags));
    }
    public static byte[] encode(byte[] input, int flags) {
        return encode(input, 0, input != null ? input.length : 0, flags);
    }
    public static byte[] encode(byte[] input, int offset, int len, int flags) {
        if (input == null || len <= 0) return new byte[0];
        StringBuilder out = new StringBuilder((len * 4) / 3 + 4);
        int b;
        for (int i = 0; i < len; i += 3) {
            b = (input[offset + i] & 0xFC) >> 2;
            out.append(CODES.charAt(b));
            b = (input[offset + i] & 0x03) << 4;
            if (i + 1 < len) {
                b |= (input[offset + i + 1] & 0xF0) >> 4;
                out.append(CODES.charAt(b));
                b = (input[offset + i + 1] & 0x0F) << 2;
                if (i + 2 < len) {
                    b |= (input[offset + i + 2] & 0xC0) >> 6;
                    out.append(CODES.charAt(b));
                    b = input[offset + i + 2] & 0x3F;
                    out.append(CODES.charAt(b));
                } else {
                    out.append(CODES.charAt(b));
                    out.append('=');
                }
            } else {
                out.append(CODES.charAt(b));
                out.append("==");
            }
        }
        return out.toString().getBytes();
    }
}
