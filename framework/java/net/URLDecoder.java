package java.net;

import java.io.UnsupportedEncodingException;

/** application/x-www-form-urlencoded decoding. */
public class URLDecoder {
    private URLDecoder() {}

    public static String decode(String s, String enc) throws UnsupportedEncodingException {
        if (enc == null) throw new NullPointerException("charsetName == null");
        return decode(s);
    }

    public static String decode(String s, java.nio.charset.Charset charset) {
        return decode(s);
    }

    /** Decodes %XX sequences as UTF-8 and '+' as a space. */
    public static String decode(String s) {
        if (s.indexOf('%') < 0 && s.indexOf('+') < 0) return s;
        StringBuilder out = new StringBuilder(s.length());
        int n = s.length();
        int i = 0;
        while (i < n) {
            char c = s.charAt(i);
            if (c == '+') {
                out.append(' ');
                i++;
            } else if (c == '%') {
                byte[] buf = new byte[(n - i) / 3];
                int len = 0;
                while (i + 2 < n + 0 && s.charAt(i) == '%' && len < buf.length) {
                    int hi = Character.digit(s.charAt(i + 1), 16);
                    int lo = Character.digit(s.charAt(i + 2), 16);
                    if (hi < 0 || lo < 0) {
                        throw new IllegalArgumentException("URLDecoder: Illegal hex characters in escape (%) pattern");
                    }
                    buf[len++] = (byte) ((hi << 4) | lo);
                    i += 3;
                }
                if (i < n && s.charAt(i) == '%' && len < buf.length) {
                    throw new IllegalArgumentException("URLDecoder: Incomplete trailing escape (%) pattern");
                }
                if (len == 0) {
                    throw new IllegalArgumentException("URLDecoder: Incomplete trailing escape (%) pattern");
                }
                out.append(new String(buf, 0, len));
            } else {
                out.append(c);
                i++;
            }
        }
        return out.toString();
    }
}
