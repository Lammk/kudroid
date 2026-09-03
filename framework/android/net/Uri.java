package android.net;

/**
 * minimal android.net.uri implementation.
 *
 * represents a uri. for kudroid minimal framework we store as string
 * and provides basic parsing helpers.
 */
public final class Uri {
    private final String mString;

    private Uri(String s) {
        mString = s;
    }

    /**
     * parses a uri from a string.
     */
    public static Uri parse(String uriString) {
        return new Uri(uriString);
    }

    /**
     * returns the string form of this uri.
     */
    public String toString() {
        return mString;
    }

    /**
     * returns the protocol (e.g. "http", "content").
     */
    public String getScheme() {
        if (mString == null) return null;
        int idx = mString.indexOf(':');
        return (idx > 0) ? mString.substring(0, idx) : null;
    }

    /**
     * returns the path part.
     */
    public String getPath() {
        if (mString == null) return null;
        int schemeIdx = mString.indexOf(':');
        int start = (schemeIdx >= 0) ? schemeIdx + 1 : 0;
        int queryIdx = mString.indexOf('?', start);
        int end = (queryIdx >= 0) ? queryIdx : mString.length();
        return mString.substring(start, end);
    }

    /**
     * returns the query part (without '?').
     */
    public String getQuery() {
        if (mString == null) return null;
        int queryIdx = mString.indexOf('?');
        return (queryIdx >= 0) ? mString.substring(queryIdx + 1) : null;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Uri)) return false;
        Uri other = (Uri) o;
        return mString == null ? other.mString == null : mString.equals(other.mString);
    }

    @Override
    public int hashCode() {
        return mString == null ? 0 : mString.hashCode();
    }

    // ── percent-encoding ─────────────────────────────────────────────────────
    //
    // Uri.encode is a static string utility rather than anything to do with a Uri instance,
    // and it is used far outside URL building: apps run it over a filename or a query
    // fragment before putting it into a path or an HTTP request. Getting the character set
    // wrong is not cosmetic — an unescaped '&' or '=' silently changes what the receiver
    // parses, and over-escaping a '/' breaks the path it was part of.
    //
    // The unreserved set is RFC 3986's, which is also what the platform uses:
    // A-Z a-z 0-9 and "_-!.~'()*". Everything else becomes %XX of its UTF-8 bytes.

    /** Percent-encodes everything outside the unreserved set. */
    public static String encode(String s) {
        return encode(s, null);
    }

    /**
     * Percent-encodes `s`, leaving the characters in `allow` alone as well.
     *
     * The two-argument form is what makes the function usable for a path: passing "/" keeps
     * the separators while still escaping the segments.
     */
    public static String encode(String s, String allow) {
        if (s == null) {
            return null;
        }
        StringBuilder out = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (isUnreserved(c) || (allow != null && allow.indexOf(c) >= 0)) {
                out.append(c);
                continue;
            }
            // Encode the UTF-8 bytes, not the char: a non-ASCII character is more than one
            // byte and encoding the code unit would produce a sequence no decoder accepts.
            appendPercentEncoded(out, s.substring(i, i + 1));
        }
        return out.toString();
    }

    /** Inverse of encode. Malformed input is passed through rather than throwing. */
    public static String decode(String s) {
        if (s == null) {
            return null;
        }
        byte[] buf = new byte[s.length()];
        int len = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            if (c == '%' && i + 2 < s.length()) {
                final int hi = hexValue(s.charAt(i + 1));
                final int lo = hexValue(s.charAt(i + 2));
                if (hi >= 0 && lo >= 0) {
                    buf[len++] = (byte) ((hi << 4) | lo);
                    i += 2;
                    continue;
                }
                // Not a valid escape. Keeping the '%' is what the platform does; dropping it
                // would silently corrupt a string that merely contains a percent sign.
            }
            if (c == '+') {
                buf[len++] = (byte) ' ';
                continue;
            }
            byte[] bytes = utf8Bytes(s.substring(i, i + 1));
            for (int k = 0; k < bytes.length && len < buf.length; k++) {
                buf[len++] = bytes[k];
            }
        }
        byte[] exact = new byte[len];
        for (int i = 0; i < len; i++) {
            exact[i] = buf[i];
        }
        return new String(exact);
    }

    private static boolean isUnreserved(char c) {
        if (c >= 'A' && c <= 'Z') return true;
        if (c >= 'a' && c <= 'z') return true;
        if (c >= '0' && c <= '9') return true;
        return "_-!.~'()*".indexOf(c) >= 0;
    }

    private static void appendPercentEncoded(StringBuilder out, String piece) {
        byte[] bytes = utf8Bytes(piece);
        for (int i = 0; i < bytes.length; i++) {
            final int b = bytes[i] & 0xFF;
            out.append('%');
            out.append(HEX[(b >> 4) & 0xF]);
            out.append(HEX[b & 0xF]);
        }
    }

    private static byte[] utf8Bytes(String s) {
        // String.getBytes() on this runtime already produces UTF-8; going through it keeps
        // one encoder rather than a second implementation that can disagree.
        return s.getBytes();
    }

    private static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    private static final char[] HEX = {
        '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
    };

    public static class Builder {
        public Builder() {}
    }

}
