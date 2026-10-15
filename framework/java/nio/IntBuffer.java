package java.nio;

/**
 * java.nio.IntBuffer — int view over a backing array.
 *
 * FMOD's callback path builds its interleaved sample buffer here
 * (ByteBuffer.asIntBuffer on the engine side of the bridge), so a missing
 * class turns every sound install into NoClassDefFoundError and the mixer
 * ends up handing Unity a null string. Follows the shape of LongBuffer: the
 * abstract shell exists for instanceof/casts, wrap() returns the anonymous
 * implementation that actually runs.
 */
public abstract class IntBuffer extends Buffer implements Comparable<IntBuffer> {
    final int[] hb;
    final int offset;

    IntBuffer(long address, int capacity, int[] hb, int offset) {
        super(address, capacity);
        this.hb = hb;
        this.offset = offset;
    }

    public static IntBuffer allocate(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException("capacity < 0");
        return wrap(new int[capacity], 0, capacity);
    }

    public static IntBuffer wrap(int[] array, int offset, int length) {
        if (array == null) throw new NullPointerException("array must not be null");
        if (offset < 0 || length < 0 || offset + length > array.length) {
            throw new IndexOutOfBoundsException("offset=" + offset + " length=" + length
                    + " array.length=" + array.length);
        }
        return new IntBuffer(0, length, array, offset) {
            public int get() { return hb[position++]; }
            public IntBuffer put(int i) { hb[position++] = i; return this; }
            public int get(int index) { return hb[offset + index]; }
            public IntBuffer put(int index, int i) { hb[offset + index] = i; return this; }
            public IntBuffer get(int[] dst, int dstOffset, int len) {
                if (len > remaining()) throw new BufferUnderflowException();
                System.arraycopy(hb, offset + position, dst, dstOffset, len);
                position += len;
                return this;
            }
            public IntBuffer get(int[] dst) { return get(dst, 0, dst.length); }
            public IntBuffer put(int[] src, int srcOffset, int len) {
                if (len > remaining()) throw new BufferOverflowException();
                System.arraycopy(src, srcOffset, hb, offset + position, len);
                position += len;
                return this;
            }
            public IntBuffer put(int[] src) { return put(src, 0, src.length); }
            public boolean isDirect() { return false; }
            public boolean isReadOnly() { return false; }
        };
    }

    public static IntBuffer wrap(int[] array) {
        return wrap(array, 0, array.length);
    }

    public abstract int get();
    public abstract IntBuffer put(int i);
    public abstract int get(int index);
    public abstract IntBuffer put(int index, int i);
    public abstract IntBuffer get(int[] dst, int dstOffset, int len);
    public abstract IntBuffer get(int[] dst);
    public abstract IntBuffer put(int[] src, int srcOffset, int len);
    public abstract IntBuffer put(int[] src);

    public final boolean hasArray() { return hb != null; }
    public final int[] array() { return hb; }
    public final int arrayOffset() { return offset; }

    public IntBuffer asReadOnlyBuffer() {
        final IntBuffer source = this;
        return new IntBuffer(address, capacity, hb, offset) {
            public int get() { return source.get(position); }
            public IntBuffer put(int i) { throw new ReadOnlyBufferException(); }
            public int get(int index) { return source.get(index); }
            public IntBuffer put(int index, int i) { throw new ReadOnlyBufferException(); }
            public IntBuffer get(int[] dst, int dstOffset, int len) {
                throw new ReadOnlyBufferException();
            }
            public IntBuffer get(int[] dst) { throw new ReadOnlyBufferException(); }
            public IntBuffer put(int[] src, int srcOffset, int len) {
                throw new ReadOnlyBufferException();
            }
            public IntBuffer put(int[] src) { throw new ReadOnlyBufferException(); }
            public boolean isDirect() { return source.isDirect(); }
            public boolean isReadOnly() { return true; }
        };
    }

    public IntBuffer slice() {
        return wrap(hb, offset + position, remaining());
    }

    public IntBuffer duplicate() {
        return wrap(hb, offset, capacity);
    }

    public int compareTo(IntBuffer that) { return 0; }
}
