package java.nio;

import java.io.IOException;

public abstract class CharBuffer extends Buffer implements Comparable<CharBuffer>, Appendable, CharSequence, Readable {
    CharBuffer(long address, int capacity) {
        super(address, capacity);
    }
    public static CharBuffer allocate(int capacity) { return null; }
    public static CharBuffer wrap(char[] array, int offset, int length) { return null; }
    public static CharBuffer wrap(char[] array) { return wrap(array, 0, array.length); }
    public static CharBuffer wrap(CharSequence csq, int start, int end) { return null; }
    public static CharBuffer wrap(CharSequence csq) { return wrap(csq, 0, csq.length()); }
    public abstract char get();
    public abstract CharBuffer put(char c);
    public abstract char get(int index);
    public abstract CharBuffer put(int index, char c);
    public final int length() { return limit - position; }
    public final char charAt(int index) { return get(position + index); }
    public abstract CharSequence subSequence(int start, int end);
    public CharBuffer append(CharSequence csq) { return put(csq.toString()); }
    public CharBuffer append(CharSequence csq, int start, int end) { return put(csq.subSequence(start, end).toString()); }
    public CharBuffer append(char c) { return put(c); }
    public final CharBuffer put(String src) { return put(src, 0, src.length()); }
    public CharBuffer put(String src, int start, int end) { return this; }
    public int read(CharBuffer target) throws IOException { return 0; }
    public int compareTo(CharBuffer that) { return 0; }
}
