package java.nio;

public abstract class ShortBuffer extends Buffer implements Comparable<ShortBuffer> {
    final short[] hb;
    final int offset;

    ShortBuffer(long address, int capacity, short[] hb, int offset) {
        super(address, capacity);
        this.hb = hb;
        this.offset = offset;
    }

    public static ShortBuffer allocate(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity < 0");
        return wrap(new short[capacity], 0, capacity);
    }

    public static ShortBuffer wrap(short[] array, int offset, int length) {
        if (array == null) throw new NullPointerException("array must not be null");
        if (offset < 0 || length < 0 || offset + length > array.length) {
            throw new IndexOutOfBoundsException("offset=" + offset + " length=" + length
                    + " array.length=" + array.length);
        }
        return new ShortBuffer(0, length, array, offset) {
            public short get() { return hb[position++]; }
            public ShortBuffer put(short s) { hb[position++] = s; return this; }
            public short get(int index) { return hb[offset + index]; }
            public ShortBuffer put(int index, short s) { hb[offset + index] = s; return this; }
            public ShortBuffer get(short[] dst, int dstOffset, int len) {
                if (len > remaining()) throw new BufferUnderflowException();
                System.arraycopy(hb, offset + position, dst, dstOffset, len);
                position += len;
                return this;
            }
            public ShortBuffer get(short[] dst) { return get(dst, 0, dst.length); }
            public ShortBuffer put(short[] src, int srcOffset, int len) {
                if (len > remaining()) throw new BufferOverflowException();
                System.arraycopy(src, srcOffset, hb, offset + position, len);
                position += len;
                return this;
            }
            public ShortBuffer put(short[] src) { return put(src, 0, src.length); }
            public boolean isDirect() { return false; }
            public boolean isReadOnly() { return false; }
        };
    }

    public static ShortBuffer wrap(short[] array) {
        return wrap(array, 0, array.length);
    }

    public abstract short get();
    public abstract ShortBuffer put(short s);
    public abstract short get(int index);
    public abstract ShortBuffer put(int index, short s);
    public abstract ShortBuffer get(short[] dst, int dstOffset, int len);
    public abstract ShortBuffer get(short[] dst);
    public abstract ShortBuffer put(short[] src, int srcOffset, int len);
    public abstract ShortBuffer put(short[] src);

    public final boolean hasArray() { return hb != null; }
    public final short[] array() { return hb; }
    public final int arrayOffset() { return offset; }

    public ShortBuffer asReadOnlyBuffer() {
        final ShortBuffer source = this;
        return new ShortBuffer(address, capacity, hb, offset) {
            public short get() { return source.get(position); }
            public ShortBuffer put(short s) { throw new ReadOnlyBufferException(); }
            public short get(int index) { return source.get(index); }
            public ShortBuffer put(int index, short s) { throw new ReadOnlyBufferException(); }
            public ShortBuffer get(short[] dst, int dstOffset, int len) { throw new ReadOnlyBufferException(); }
            public ShortBuffer get(short[] dst) { throw new ReadOnlyBufferException(); }
            public ShortBuffer put(short[] src, int srcOffset, int len) { throw new ReadOnlyBufferException(); }
            public ShortBuffer put(short[] src) { throw new ReadOnlyBufferException(); }
            public boolean isDirect() { return source.isDirect(); }
            public boolean isReadOnly() { return true; }
        };
    }

    public ShortBuffer slice() { return wrap(hb, offset + position, remaining()); }
    public ShortBuffer duplicate() { return wrap(hb, offset, capacity); }
    public int compareTo(ShortBuffer that) { return 0; }
}
