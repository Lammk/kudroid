package java.lang;

/**
 * Character buffer allows string concatenation. javac translates all `a + b` operations on String
 * into StringBuilder so this class must run as soon as possible.
 */
public final class StringBuilder implements CharSequence, Appendable {

    private char[] buf;
    private int count;

    public StringBuilder() {
        buf = new char[16];
    }

    public StringBuilder(int capacity) {
        buf = new char[capacity < 1 ? 16 : capacity];
    }

    public StringBuilder(String str) {
        buf = new char[str.length() + 16];
        append(str);
    }

    public StringBuilder(CharSequence seq) {
        buf = new char[seq.length() + 16];
        append(seq);
    }

    private void ensure(int minCapacity) {
        if (minCapacity <= buf.length) {
            return;
        }
        int newLen = buf.length * 2 + 2;
        if (newLen < minCapacity) {
            newLen = minCapacity;
        }
        char[] next = new char[newLen];
        System.arraycopy(buf, 0, next, 0, count);
        buf = next;
    }

    public int length() {
        return count;
    }

    public int capacity() {
        return buf.length;
    }

    public void ensureCapacity(int minimumCapacity) {
        ensure(minimumCapacity);
    }

    public void trimToSize() {
    }

    public char charAt(int index) {
        if (index < 0 || index >= count) {
            throw new StringIndexOutOfBoundsException(index);
        }
        return buf[index];
    }

    public void setCharAt(int index, char c) {
        if (index < 0 || index >= count) {
            throw new StringIndexOutOfBoundsException(index);
        }
        buf[index] = c;
    }

    public void setLength(int newLength) {
        if (newLength < 0) {
            throw new StringIndexOutOfBoundsException(newLength);
        }
        ensure(newLength);
        for (int i = count; i < newLength; i++) {
            buf[i] = '\u0000';
        }
        count = newLength;
    }

    public StringBuilder append(char c) {
        ensure(count + 1);
        buf[count++] = c;
        return this;
    }

    public StringBuilder append(String str) {
        if (str == null) {
            str = "null";
        }
        int n = str.length();
        ensure(count + n);
        for (int i = 0; i < n; i++) {
            buf[count + i] = str.charAt(i);
        }
        count += n;
        return this;
    }

    public StringBuilder append(CharSequence csq) {
        return csq == null ? append("null") : append(csq, 0, csq.length());
    }

    public StringBuilder append(CharSequence csq, int start, int end) {
        if (csq == null) {
            return append("null");
        }
        ensure(count + (end - start));
        for (int i = start; i < end; i++) {
            buf[count++] = csq.charAt(i);
        }
        return this;
    }

    public StringBuilder append(Object obj) {
        return append(obj == null ? "null" : obj.toString());
    }

    public StringBuilder append(StringBuilder sb) {
        return sb == null ? append("null") : append(sb.toString());
    }

    public StringBuilder append(boolean b) {
        return append(b ? "true" : "false");
    }

    public StringBuilder append(int i) {
        return append(Integer.toString(i));
    }

    public StringBuilder append(long l) {
        return append(Long.toString(l));
    }

    public StringBuilder append(float f) {
        return append(Float.toString(f));
    }

    public StringBuilder append(double d) {
        return append(Double.toString(d));
    }

    public StringBuilder append(char[] chars) {
        return append(chars, 0, chars.length);
    }

    public StringBuilder append(char[] chars, int offset, int len) {
        ensure(count + len);
        System.arraycopy(chars, offset, buf, count, len);
        count += len;
        return this;
    }

    public StringBuilder appendCodePoint(int codePoint) {
        return append((char) codePoint);
    }

    public StringBuilder insert(int offset, String str) {
        if (str == null) {
            str = "null";
        }
        if (offset < 0 || offset > count) {
            throw new StringIndexOutOfBoundsException(offset);
        }
        int n = str.length();
        ensure(count + n);
        System.arraycopy(buf, offset, buf, offset + n, count - offset);
        for (int i = 0; i < n; i++) {
            buf[offset + i] = str.charAt(i);
        }
        count += n;
        return this;
    }

    public StringBuilder insert(int offset, char c) {
        return insert(offset, String.valueOf(c));
    }

    public StringBuilder insert(int offset, int i) {
        return insert(offset, Integer.toString(i));
    }

    public StringBuilder insert(int offset, Object obj) {
        return insert(offset, obj == null ? "null" : obj.toString());
    }

    public StringBuilder delete(int start, int end) {
        if (start < 0 || start > count || start > end) {
            throw new StringIndexOutOfBoundsException(start);
        }
        if (end > count) {
            end = count;
        }
        System.arraycopy(buf, end, buf, start, count - end);
        count -= end - start;
        return this;
    }

    public StringBuilder deleteCharAt(int index) {
        return delete(index, index + 1);
    }

    public StringBuilder replace(int start, int end, String str) {
        delete(start, end);
        return insert(start, str);
    }

    public StringBuilder reverse() {
        for (int i = 0, j = count - 1; i < j; i++, j--) {
            char t = buf[i];
            buf[i] = buf[j];
            buf[j] = t;
        }
        return this;
    }

    public int indexOf(String str) {
        return toString().indexOf(str);
    }

    public int indexOf(String str, int fromIndex) {
        return toString().indexOf(str, fromIndex);
    }

    public int lastIndexOf(String str) {
        return toString().lastIndexOf(str);
    }

    public String substring(int start) {
        return substring(start, count);
    }

    public String substring(int start, int end) {
        if (start < 0 || end > count || start > end) {
            throw new StringIndexOutOfBoundsException(start);
        }
        return new String(buf, start, end - start);
    }

    public CharSequence subSequence(int start, int end) {
        return substring(start, end);
    }

    public String toString() {
        return new String(buf, 0, count);
    }
}
