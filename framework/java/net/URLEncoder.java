package java.net;

import java.io.UnsupportedEncodingException;

/** application/x-www-form-urlencoded encoding. */
public class URLEncoder {
    private static final char[] HEX = "0123456789ABCDEF".toCharArray();

    private URLEncoder() {}

    public static String encode(String s, String enc) throws UnsupportedEncodingException {
        if (enc == null) throw new NullPointerException("charsetName == null");
        return encode(s);
    }

    public static String encode(String s, java.nio.charset.Charset charset) {
        return encode(s);
    }

    /** Encodes as UTF-8, the only charset the platform's string conversion offers. */
    public static String encode(String s) {
        StringBuilder out = null;
        byte[] bytes = null;
        int n = s.length();
        for (int i = 0; i < n; i++) {
            char c = s.charAt(i);
            if (isSafe(c)) {
                if (out != null) out.append(c);
                continue;
            }
            if (out == null) {
                out = new StringBuilder(n + 16);
                out.append(s.substring(0, i));
            }
            if (c == ' ') {
                out.append('+');
                continue;
            }
            // Take the whole run of unsafe characters so surrogate pairs encode together.
            int end = i + 1;
            while (end < n && !isSafe(s.charAt(end)) && s.charAt(end) != ' ') end++;
            bytes = s.substring(i, end).getBytes();
            for (int b = 0; b < bytes.length; b++) {
                out.append('%').append(HEX[(bytes[b] >> 4) & 0xf]).append(HEX[bytes[b] & 0xf]);
            }
            i = end - 1;
        }
        return out == null ? s : out.toString();
    }

    private static boolean isSafe(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                || c == '.' || c == '-' || c == '*' || c == '_';
    }
}
