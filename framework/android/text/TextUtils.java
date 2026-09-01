package android.text;

public class TextUtils {
    public enum TruncateAt {
        START,
        MIDDLE,
        END,
        MARQUEE,
        END_SMALL
    }

    public static boolean isEmpty(CharSequence str) {
        return str == null || str.length() == 0;
    }

    public static boolean equals(CharSequence a, CharSequence b) {
        if (a == b) return true;
        int length;
        if (a != null && b != null && (length = a.length()) == b.length()) {
            if (a instanceof String && b instanceof String) {
                return a.equals(b);
            } else {
                for (int i = 0; i < length; i++) {
                    if (a.charAt(i) != b.charAt(i)) return false;
                }
                return true;
            }
        }
        return false;
    }

    public static CharSequence concat(CharSequence... pieces) {
        if (pieces.length == 0) return "";
        if (pieces.length == 1) return pieces[0];
        StringBuilder sb = new StringBuilder();
        for (CharSequence piece : pieces) {
            if (piece != null) sb.append(piece);
        }
        return sb.toString();
    }

    public static String join(CharSequence delimiter, Object[] tokens) {
        if (tokens == null || tokens.length == 0) return "";
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (Object token : tokens) {
            if (first) first = false;
            else sb.append(delimiter);
            sb.append(token);
        }
        return sb.toString();
    }

    public static String join(CharSequence delimiter, Iterable tokens) {
        if (tokens == null) return "";
        StringBuilder sb = new StringBuilder();
        boolean first = true;
        for (Object token : tokens) {
            if (first) first = false;
            else sb.append(delimiter);
            sb.append(token);
        }
        return sb.toString();
    }

    public static int indexOf(CharSequence s, char ch, int start, int end) {
        if (s == null) return -1;
        for (int i = start; i < end && i < s.length(); i++) {
            if (s.charAt(i) == ch) return i;
        }
        return -1;
    }
}
