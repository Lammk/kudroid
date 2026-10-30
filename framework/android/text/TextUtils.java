package android.text;

public class TextUtils {
    /** Writes/reads a CharSequence as its text; spans are not carried. */
    public static final android.os.Parcelable.Creator<CharSequence> CHAR_SEQUENCE_CREATOR =
            new android.os.Parcelable.Creator<CharSequence>() {
                @Override
                public CharSequence createFromParcel(android.os.Parcel p) {
                    return p.readString();
                }

                @Override
                public CharSequence[] newArray(int size) {
                    return new CharSequence[size];
                }
            };

    public static void writeToParcel(CharSequence cs, android.os.Parcel p, int flags) {
        p.writeString(cs != null ? cs.toString() : null);
    }

    public enum TruncateAt {
        START,
        MIDDLE,
        END,
        MARQUEE,
        END_SMALL
    }

    /** View.LAYOUT_DIRECTION_RTL (1) for right-to-left scripts, else LTR (0). */
    public static int getLayoutDirectionFromLocale(java.util.Locale locale) {
        if (locale == null) return 0;
        final String language = locale.getLanguage();
        switch (language) {
            case "ar": case "fa": case "he": case "iw": case "ur": case "yi":
            case "ps": case "sd": case "ug": case "dv": case "ckb":
                return 1;
            default:
                return 0;
        }
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
