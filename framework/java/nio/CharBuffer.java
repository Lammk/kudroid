package java.nio;

import java.io.IOException;

public abstract class CharBuffer extends Buffer implements Comparable<CharBuffer>, Appendable, CharSequence, Readable {
    CharBuffer(long address, int capacity) {
        super(address, capacity);
    }
    public static CharBuffer allocate(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity");
        return wrap(new char[capacity]);
    }
    public static CharBuffer wrap(char[] array, int offset, int length) {
        if (array == null) throw new NullPointerException("array");
        if (offset < 0 || length < 0 || offset > array.length - length) throw new IndexOutOfBoundsException();
        return new SequenceBuffer(null, array, offset, offset + length);
    }
    public static CharBuffer wrap(char[] array) { return wrap(array, 0, array.length); }
    public static CharBuffer wrap(CharSequence csq, int start, int end) {
        if (csq == null) throw new NullPointerException("sequence");
        if (start < 0 || end < start || end > csq.length()) throw new IndexOutOfBoundsException();
        return new SequenceBuffer(csq, null, start, end);
    }
    public static CharBuffer wrap(CharSequence csq) { return wrap(csq, 0, csq.length()); }
    public abstract char get();
    public abstract CharBuffer put(char c);
    public abstract char get(int index);
    public abstract CharBuffer put(int index, char c);
    public final int length() { return limit - position; }
    public final char charAt(int index) {
        if (index < 0 || index >= remaining()) throw new IndexOutOfBoundsException();
        return get(position + index);
    }
    public abstract CharSequence subSequence(int start, int end);
    public CharBuffer append(CharSequence csq) { return put(csq == null ? "null" : csq.toString()); }
    public CharBuffer append(CharSequence csq, int start, int end) {
        return put((csq == null ? "null" : csq).subSequence(start, end).toString());
    }
    public CharBuffer append(char c) { return put(c); }
    public final CharBuffer put(String src) { return put(src, 0, src.length()); }
    public CharBuffer put(String src, int start, int end) {
        if (start < 0 || end < start || end > src.length()) throw new IndexOutOfBoundsException();
        if (isReadOnly()) throw new ReadOnlyBufferException();
        if (end - start > remaining()) throw new BufferOverflowException();
        for (int i = start; i < end; i++) put(src.charAt(i));
        return this;
    }
    public int read(CharBuffer target) throws IOException {
        if (target == this) throw new IllegalArgumentException("target == source");
        if (!hasRemaining()) return -1;
        int count = Math.min(remaining(), target.remaining());
        for (int i = 0; i < count; i++) {
            target.put(get(position()));
            position(position() + 1);
        }
        return count;
    }
    public int compareTo(CharBuffer that) {
        int count = Math.min(remaining(), that.remaining());
        for (int i = 0; i < count; i++) {
            int difference = charAt(i) - that.charAt(i);
            if (difference != 0) return difference;
        }
        return remaining() - that.remaining();
    }
    public boolean isReadOnly() { return false; }
    public boolean isDirect() { return false; }
    @Override public String toString() {
        char[] chars = new char[remaining()];
        for (int i = 0; i < chars.length; i++) chars[i] = charAt(i);
        return new String(chars);
    }

    private static final class SequenceBuffer extends CharBuffer {
        private final CharSequence sequence;
        private final char[] array;
        SequenceBuffer(CharSequence sequence, char[] array, int start, int end) {
            super(0, array != null ? array.length : sequence.length());
            this.sequence = sequence;
            this.array = array;
            position(start);
            limit(end);
        }
        @Override public boolean isReadOnly() { return array == null; }
        @Override public char get() {
            if (!hasRemaining()) throw new BufferUnderflowException();
            char value = get(position());
            position(position() + 1);
            return value;
        }
        @Override public char get(int index) {
            if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
            return array != null ? array[index] : sequence.charAt(index);
        }
        @Override public CharBuffer put(char value) {
            if (isReadOnly()) throw new ReadOnlyBufferException();
            if (!hasRemaining()) throw new BufferOverflowException();
            array[position()] = value;
            position(position() + 1);
            return this;
        }
        @Override public CharBuffer put(int index, char value) {
            if (isReadOnly()) throw new ReadOnlyBufferException();
            if (index < 0 || index >= limit()) throw new IndexOutOfBoundsException();
            array[index] = value;
            return this;
        }
        @Override public CharSequence subSequence(int start, int end) {
            if (start < 0 || end < start || end > remaining()) throw new IndexOutOfBoundsException();
            return new SequenceBuffer(sequence, array, position() + start, position() + end);
        }
    }
}
