package java.lang;

/**
 * Immutable strings. The real data is in KuART's DexString (raw UTF-8).
 * is not a Java field, so all character access is via native.
 */
public final class String implements CharSequence, Comparable<String> {

    public String() {
        initEmpty();
    }

    public String(String original) {
        initCopy(original);
    }

    public String(char[] value) {
        initChars(value, 0, value.length);
    }

    public String(char[] value, int offset, int count) {
        initChars(value, offset, count);
    }

    public String(byte[] bytes) {
        initBytes(bytes, 0, bytes.length);
    }

    public String(byte[] bytes, int offset, int length) {
        initBytes(bytes, offset, length);
    }

    public String(byte[] bytes, String charsetName) {
        initBytes(bytes, 0, bytes.length);
    }

    private native void initEmpty();

    private native void initCopy(String other);

    private native void initChars(char[] value, int offset, int count);

    private native void initBytes(byte[] bytes, int offset, int length);

    public native int length();

    public native char charAt(int index);

    public native int indexOf(int ch, int fromIndex);

    public native int lastIndexOf(int ch, int fromIndex);

    public native int indexOf(String str, int fromIndex);

    public native int lastIndexOf(String str, int fromIndex);

    public native String substring(int beginIndex, int endIndex);

    public native String concat(String str);

    public native String replace(char oldChar, char newChar);

    public native String replace(CharSequence target, CharSequence replacement);

    public native String toLowerCase();

    public native String toUpperCase();

    public native String trim();

    public native boolean equalsIgnoreCase(String other);

    public native int compareTo(String other);

    public native String intern();

    public native byte[] getBytes();

    public native char[] toCharArray();

    public boolean isEmpty() {
        return length() == 0;
    }

    public int indexOf(int ch) {
        return indexOf(ch, 0);
    }

    public int lastIndexOf(int ch) {
        return lastIndexOf(ch, length());
    }

    public int indexOf(String str) {
        return indexOf(str, 0);
    }

    public int lastIndexOf(String str) {
        return lastIndexOf(str, length());
    }

    public String substring(int beginIndex) {
        return substring(beginIndex, length());
    }

    public boolean contains(CharSequence s) {
        return indexOf(s.toString(), 0) >= 0;
    }

    public boolean startsWith(String prefix) {
        return startsWith(prefix, 0);
    }

    public boolean startsWith(String prefix, int offset) {
        int n = prefix.length();
        if (offset < 0 || offset + n > length()) {
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (charAt(offset + i) != prefix.charAt(i)) {
                return false;
            }
        }
        return true;
    }

    public boolean endsWith(String suffix) {
        return startsWith(suffix, length() - suffix.length());
    }

    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (!(other instanceof String)) {
            return false;
        }
        String s = (String) other;
        int n = length();
        if (n != s.length()) {
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (charAt(i) != s.charAt(i)) {
                return false;
            }
        }
        return true;
    }

    public boolean contentEquals(CharSequence cs) {
        return equals(cs.toString());
    }

    public int hashCode() {
        int h = 0;
        int n = length();
        for (int i = 0; i < n; i++) {
            h = 31 * h + charAt(i);
        }
        return h;
    }

    public String toString() {
        return this;
    }

    public CharSequence subSequence(int start, int end) {
        return substring(start, end);
    }

    public void getChars(int srcBegin, int srcEnd, char[] dst, int dstBegin) {
        for (int i = srcBegin; i < srcEnd; i++) {
            dst[dstBegin + i - srcBegin] = charAt(i);
        }
    }

    public byte[] getBytes(String charsetName) {
        return getBytes();
    }

    public String[] split(String regex) {
        return split(regex, 0);
    }

    /** Split by fixed string only — KuART does not have a regex engine. */
    public String[] split(String separator, int limit) {
        if (separator.length() == 0) {
            return new String[] { this };
        }
        java.util.ArrayList<String> parts = new java.util.ArrayList<String>();
        int start = 0;
        while (true) {
            if (limit > 0 && parts.size() == limit - 1) {
                break;
            }
            int idx = indexOf(separator, start);
            if (idx < 0) {
                break;
            }
            parts.add(substring(start, idx));
            start = idx + separator.length();
        }
        parts.add(substring(start, length()));
        if (limit == 0) {
            while (parts.size() > 1 && parts.get(parts.size() - 1).length() == 0) {
                parts.remove(parts.size() - 1);
            }
        }
        String[] out = new String[parts.size()];
        for (int i = 0; i < out.length; i++) {
            out[i] = parts.get(i);
        }
        return out;
    }

    public boolean matches(String regex) {
        return equals(regex);
    }

    public String replaceAll(String regex, String replacement) {
        return replace(regex, replacement);
    }

    public String replaceFirst(String regex, String replacement) {
        int idx = indexOf(regex, 0);
        if (idx < 0) {
            return this;
        }
        return substring(0, idx) + replacement + substring(idx + regex.length());
    }

    public static String valueOf(Object obj) {
        return obj == null ? "null" : obj.toString();
    }

    public static String valueOf(boolean b) {
        return b ? "true" : "false";
    }

    public static String valueOf(char c) {
        return new String(new char[] { c });
    }

    public static String valueOf(int i) {
        return Integer.toString(i);
    }

    public static String valueOf(long l) {
        return Long.toString(l);
    }

    public static String valueOf(float f) {
        return Float.toString(f);
    }

    public static String valueOf(double d) {
        return Double.toString(d);
    }

    public static String valueOf(char[] data) {
        return new String(data);
    }

    public static String valueOf(char[] data, int offset, int count) {
        return new String(data, offset, count);
    }

    public static String copyValueOf(char[] data) {
        return new String(data);
    }

    public static String format(String format, Object... args) {
        return Formatter.format(format, args);
    }

    public static String format(java.util.Locale locale, String format, Object... args) {
        return Formatter.format(format, args);
    }

    public static String join(CharSequence delimiter, CharSequence... elements) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < elements.length; i++) {
            if (i > 0) {
                sb.append(delimiter);
            }
            sb.append(elements[i]);
        }
        return sb.toString();
    }
}
